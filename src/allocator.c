/*
 * Реализация аллокатора (задание).
 *
 * Идея простыми словами: есть большой кусок памяти (пул) и «карта занятости»
 * — bitmap, где каждый бит соответствует одному блоку по 4 KiB.
 * Нужно выделить запрошенное число байт — округляем вверх до целых блоков,
 * ищем первую длинную цепочку свободных битов (first-fit), помечаем их
 * занятыми и возвращаем адрес внутри пула.
 *
 * При освобождении по указателю нужно знать, сколько блоков было выдано.
 * Под спинлоком нельзя вызывать kmalloc (планировщик может уснуть), поэтому
 * таблица активных выделений — статический массив: в каждой строке лежит
 * struct allocation_info из задания плюс адрес и флаг «запись используется».
 */

#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/slab.h>
#include <linux/vmalloc.h>
#include <linux/string.h>

#include "allocator.h"
#include "bitmap.h"

/*
 * g_alloc — единственный экземпляр struct memory_allocator на весь модуль:
 * указатели на bitmap и пул, размеры блоков, спинлок.
 */
static struct memory_allocator g_alloc;

/*
 * Одна строка таблицы учёта выделений.
 * Внутри лежит struct allocation_info из задания: start_block и num_blocks.
 * Дополнительно храним выданный указатель и флаг «слот занят», чтобы по ptr
 * найти дескриптор при free.
 */
typedef struct {
	bool in_use; /* true, если слот занят записью об активном выделении */
	void *ptr; /* адрес внутри memory_pool, который мы вернули пользователю */
	struct allocation_info info; /* start_block и num_blocks по заданию */
} allocation_table_row_t;

/*
 * g_track — таблица активных выделений: по одной строке на каждое успешное
 * allocator_alloc, пока не вызвали allocator_free для этого ptr.
 */
static allocation_table_row_t g_track[ALLOCATOR_TOTAL_BLOCKS];

/*
 * Занять свободную строку таблицы.
 * ptr         — адрес, который вернём пользователю;
 * start_block — первый блок выделения в bitmap;
 * num_blocks  — сколько блоков подряд занято.
 */
static int track_add_locked(void *ptr, size_t start_block, size_t num_blocks)
{
	size_t i; /* номер слота в g_track, ищем первый свободный */

	for (i = 0; i < ALLOCATOR_TOTAL_BLOCKS; i++) {
		if (!g_track[i].in_use) {
			g_track[i].ptr = ptr;
			g_track[i].info.start_block = start_block;
			g_track[i].info.num_blocks = num_blocks;
			g_track[i].in_use = true;
			return 0;
		}
	}
	return -1;
}

/*
 * Найти строку по точному значению указателя (тот же адрес, что выдали ранее).
 * ptr — указатель, полученный из allocator_alloc.
 */
static allocation_table_row_t *track_find_locked(void *ptr)
{
	size_t i; /* перебор слотов, пока ptr не совпадёт с сохранённым */

	if (!ptr)
		return NULL;

	for (i = 0; i < ALLOCATOR_TOTAL_BLOCKS; i++) {
		if (g_track[i].in_use && g_track[i].ptr == ptr)
			return &g_track[i];
	}
	return NULL;
}

/*
 * Освободить строку таблицы после снятия битов в bitmap.
 * row — указатель на найденную ранее строку g_track (не NULL).
 */
static void track_remove_locked(allocation_table_row_t *row)
{
	row->in_use = false;
	row->ptr = NULL;
	row->info.start_block = 0;
	row->info.num_blocks = 0;
}

/*
 * allocator_init — Инициализация аллокатора и выделение памяти.
 *
 * Запрашиваем у ядра два буфера: маленький под bitmap и большой под пул.
 * Если второй запрос не удался, откатываем первый. Таблица g_track обнуляется
 * здесь же, чтобы не остаться с мусором при повторной загрузке модуля.
 */
int allocator_init(void)
{
	unsigned long flags; /* под spinlock */

	memset(&g_alloc, 0, sizeof(g_alloc));
	memset(g_track, 0, sizeof(g_track));

	g_alloc.total_blocks = ALLOCATOR_TOTAL_BLOCKS;
	g_alloc.block_size = ALLOCATOR_BLOCK_SIZE;
	spin_lock_init(&g_alloc.lock);

	g_alloc.bitmap = kzalloc(ALLOCATOR_BITMAP_BYTES, GFP_KERNEL);
	if (!g_alloc.bitmap)
		return -1;

	g_alloc.memory_pool = vmalloc(ALLOCATOR_TOTAL_MEMORY);
	if (!g_alloc.memory_pool) {
		kfree(g_alloc.bitmap);
		g_alloc.bitmap = NULL;
		return -1;
	}

	/* Все блоки свободны: нулевой bitmap (kzalloc уже дал нули, повтор безопасен). */
	spin_lock_irqsave(&g_alloc.lock, flags);
	memset(g_alloc.bitmap, 0, ALLOCATOR_BITMAP_BYTES);
	spin_unlock_irqrestore(&g_alloc.lock, flags);

	return 0;
}

/*
 * allocator_alloc — Выделить блок памяти заданного размера.
 *
 * Ноль байт и слишком большой запрос сразу отклоняем.
 * Остальное переводим в число блоков, ищем first-fit, помечаем биты,
 * считаем адрес в пуле и регистрируем выделение в таблице.
 * Если таблица внезапно заполнена (крайне редко), откатываем биты в bitmap.
 */
void *allocator_alloc(size_t bytes)
{
	unsigned long flags; /* под spinlock */
	size_t num_blocks;   /* сколько блоков по 4 KiB нужно под bytes */
	size_t start = 0;    /* индекс первого блока найденного first-fit окна */
	void *ptr;           /* вычисленный адрес начала выделения в пуле */

	if (bytes == 0)
		return NULL;

	num_blocks = DIV_ROUND_UP(bytes, g_alloc.block_size);
	if (num_blocks == 0 || num_blocks > g_alloc.total_blocks)
		return NULL;

	spin_lock_irqsave(&g_alloc.lock, flags);

	if (bitmap_find_first_fit(g_alloc.bitmap, g_alloc.total_blocks,
				   num_blocks, &start) != 0) {
		spin_unlock_irqrestore(&g_alloc.lock, flags);
		return NULL;
	}

	bitmap_set_range(g_alloc.bitmap, start, num_blocks);
	ptr = (char *)g_alloc.memory_pool + start * g_alloc.block_size;

	if (track_add_locked(ptr, start, num_blocks) != 0) {
		bitmap_clear_range(g_alloc.bitmap, start, num_blocks);
		spin_unlock_irqrestore(&g_alloc.lock, flags);
		return NULL;
	}

	spin_unlock_irqrestore(&g_alloc.lock, flags);
	return ptr;
}

/*
 * allocator_free — Освободить блок памяти.
 *
 * Нулевой указатель — неверный параметр. Ищем запись в таблице: если её нет,
 * либо указатель чужой, либо повторное освобождение (в таблице уже нет строки).
 * Иначе снимаем подряд идущие биты в bitmap согласно allocation_info.
 */
int allocator_free(void *ptr)
{
	unsigned long flags;         /* под spinlock */
	allocation_table_row_t *row; /* найденная строка таблицы или NULL */
	size_t start;               /* копия info.start_block для bitmap_clear_range */
	size_t n;                   /* копия info.num_blocks — сколько бит снять */

	if (!ptr)
		return ALLOC_INVALID;

	spin_lock_irqsave(&g_alloc.lock, flags);

	row = track_find_locked(ptr);
	if (!row) {
		spin_unlock_irqrestore(&g_alloc.lock, flags);
		return ALLOC_NOT_FOUND;
	}

	start = row->info.start_block;
	n = row->info.num_blocks;

	if (start + n > g_alloc.total_blocks) {
		spin_unlock_irqrestore(&g_alloc.lock, flags);
		return ALLOC_INVALID;
	}

	bitmap_clear_range(g_alloc.bitmap, start, n);
	track_remove_locked(row);

	spin_unlock_irqrestore(&g_alloc.lock, flags);
	return ALLOC_OK;
}

/*
 * allocator_get_stats — Получить статистику аллокатора.
 *
 * Считаем, сколько битов занято, сколько блоков свободно, переводим в байты.
 * Процент фрагментации: сравниваем весь свободный объём с самым длинным
 * непрерывным свободным участком; если вся свободная память одним куском,
 * фрагментация 0%.
 */
struct stats_info allocator_get_stats(void)
{
	unsigned long flags;       /* под spinlock */
	struct stats_info s;       /* результат, заполняем по полям задания */
	size_t allocated_blocks;   /* сколько бит «занято» в сумме */
	size_t max_free_run;       /* длина самой длинной цепочки свободных блоков */
	size_t free_blocks;        /* total_blocks минус allocated_blocks */

	memset(&s, 0, sizeof(s));

	spin_lock_irqsave(&g_alloc.lock, flags);

	if (!g_alloc.bitmap || !g_alloc.memory_pool) {
		spin_unlock_irqrestore(&g_alloc.lock, flags);
		return s;
	}

	allocated_blocks =
		bitmap_count_allocated(g_alloc.bitmap, g_alloc.total_blocks);
	max_free_run =
		bitmap_max_free_run(g_alloc.bitmap, g_alloc.total_blocks);
	free_blocks = g_alloc.total_blocks - allocated_blocks;

	s.total_blocks = g_alloc.total_blocks;
	s.allocated_blocks = allocated_blocks;
	s.free_blocks = free_blocks;
	s.total_memory = g_alloc.total_blocks * g_alloc.block_size;
	s.allocated_memory = allocated_blocks * g_alloc.block_size;
	s.free_memory = free_blocks * g_alloc.block_size;

	if (s.free_memory > 0) {
		/* свободные байты, которые не входят в один самый длинный непрерывный кусок */
		size_t frag_bytes = s.free_memory - max_free_run * g_alloc.block_size;

		s.fragmentation_percent = (100U * frag_bytes) / s.free_memory;
		if (s.fragmentation_percent > 100U)
			s.fragmentation_percent = 100U;
	} else {
		s.fragmentation_percent = 0;
	}

	spin_unlock_irqrestore(&g_alloc.lock, flags);
	return s;
}

/*
 * allocator_cleanup — Очистить аллокатор и освободить память.
 *
 * Таблицу выделений обнуляем под локом; bitmap и пул отдаём ядру через
 * kfree/vfree. После выгрузки модуля к этим указателям обращаться нельзя.
 */
void allocator_cleanup(void)
{
	unsigned long flags; /* под spinlock */

	spin_lock_irqsave(&g_alloc.lock, flags);
	memset(g_track, 0, sizeof(g_track));
	spin_unlock_irqrestore(&g_alloc.lock, flags);

	vfree(g_alloc.memory_pool);
	kfree(g_alloc.bitmap);
	g_alloc.memory_pool = NULL;
	g_alloc.bitmap = NULL;
}

/*
 * allocator_format_bitmap_info — строка для чтения параметра bitmap_info.
 *
 * Кратко печатаем число блоков, сколько бит занято, и начало «картинки»:
 * символ для занятого блока и точка для свободного (как в примере задания).
 */
int allocator_format_bitmap_info(char *buf, size_t buflen)
{
	unsigned long flags;   /* под spinlock */
	const size_t preview = 256; /* сколько первых блоков рисуем символами X/. */
	size_t b;              /* индекс блока в цикле вывода превью */
	int len = 0;           /* сколько байт уже записано в buf */
	int r;                 /* результат очередного scnprintf (может быть < 0) */

	if (!buf || buflen < 4)
		return 0;

	spin_lock_irqsave(&g_alloc.lock, flags);

	if (!g_alloc.bitmap) {
		spin_unlock_irqrestore(&g_alloc.lock, flags);
		return scnprintf(buf, buflen, "uninitialized\n");
	}

	r = scnprintf(buf + len, buflen - (size_t)len,
		      "blocks=%zu used_bits=%zu [",
		      g_alloc.total_blocks,
		      bitmap_count_allocated(g_alloc.bitmap,
					     g_alloc.total_blocks));
	if (r < 0) {
		spin_unlock_irqrestore(&g_alloc.lock, flags);
		return 0;
	}
	len += r;

	for (b = 0; b < preview && b < g_alloc.total_blocks; b++) {
		char c; /* один символ картинки: занят / свободен */

		c = bitmap_test_bit(g_alloc.bitmap, b) ? 'X' : '.';

		if ((size_t)len + 2 >= buflen)
			break;
		buf[len++] = c;
	}
	if (g_alloc.total_blocks > preview && (size_t)len + 4 < buflen) {
		r = scnprintf(buf + len, buflen - (size_t)len, "...");
		if (r > 0)
			len += r;
	}

	if ((size_t)len + 2 < buflen)
		len += scnprintf(buf + len, buflen - (size_t)len, "]\n");

	spin_unlock_irqrestore(&g_alloc.lock, flags);
	return len;
}
