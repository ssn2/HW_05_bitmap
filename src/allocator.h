#ifndef _KERNEL_ALLOCATOR_H
#define _KERNEL_ALLOCATOR_H

/*
 * Заголовок аллокатора памяти на bitmap (задание).
 *
 * Здесь объявлены все структуры и константы из формулировки задания:
 * общий пул 10 MiB, блок 4 KiB, 2560 блоков, учёт через bitmap,
 * синхронизация через spinlock, коды ошибок ALLOC_*.
 */

#include <linux/spinlock.h>
#include <linux/types.h>

/*
 * Коды возврата (задание, раздел «Обработка ошибок»).
 * allocator_init сообщает об ошибке через -1;
 * allocator_free использует эти константы.
 */
#define ALLOC_OK        0  /* Операция успешна */
#define ALLOC_NOMEM     -1 /* Недостаточно памяти */
#define ALLOC_INVALID   -2 /* Неверный параметр */
#define ALLOC_NOT_FOUND -3 /* Блок не найден при освобождении */

/* 10 MiB = 10485760 байт; блок 4096 байт → 2560 блоков; bitmap 320 байт. */
#define ALLOCATOR_TOTAL_MEMORY  (10U * 1024U * 1024U) /* размер пула: 10 MiB */
#define ALLOCATOR_BLOCK_SIZE    4096U               /* один блок: 4 KiB */
#define ALLOCATOR_TOTAL_BLOCKS  2560U               /* блоков в пуле по заданию */
#define ALLOCATOR_BITMAP_BYTES  ((ALLOCATOR_TOTAL_BLOCKS + 7) / 8) /* 320 байт под 2560 бит */

/*
 * Основная структура аллокатора (задание, п. 1.1).
 * Поле bitmap — по одному биту на блок пула; memory_pool — непрерывная
 * область, из которой пользователю возвращаются смещения в виде указателей.
 */
struct memory_allocator {
	unsigned char *bitmap; /* Bitmap для отслеживания блоков (1 бит = 1 блок) */
	void *memory_pool;     /* Пул памяти для выделения */
	size_t total_blocks;   /* Общее количество блоков */
	size_t block_size;     /* Размер одного блока в байтах */
	spinlock_t lock;       /* Спинлок для синхронизации доступа */
};

/*
 * Дескриптор одной выделенной области (задание, п. 1.2).
 * Хранит, с какого блока начинается регион и сколько блоков подряд занято.
 * Сам указатель void* в задании в этой структуре не перечислён — его
 * хранит служебная таблица в allocator.c вместе с этим дескриптором.
 */
struct allocation_info {
	size_t start_block; /* Индекс начального блока */
	size_t num_blocks;  /* Количество выделенных блоков */
};

/*
 * Статистика аллокатора (задание, п. 3).
 * Заполняется allocator_get_stats() под защитой спинлока.
 */
struct stats_info {
	size_t total_blocks;          /* Общее количество блоков */
	size_t free_blocks;           /* Количество свободных блоков */
	size_t allocated_blocks;      /* Количество выделенных блоков */
	size_t total_memory;          /* Общий объем памяти в байтах */
	size_t free_memory;           /* Объем свободной памяти в байтах */
	size_t allocated_memory;      /* Объем выделенной памяти в байтах */
	size_t fragmentation_percent; /* Процент фрагментации */
};

/*
 * Инициализация аллокатора и выделение памяти (0 — успех, -1 — ошибка).
 * Внешних параметров нет: размеры пула заданы макросами ALLOCATOR_*.
 */
int allocator_init(void);

/*
 * Выделить блок памяти заданного размера (NULL при ошибке).
 * bytes — сколько байт запросил пользователь (0 и слишком много отклоняются).
 */
void *allocator_alloc(size_t bytes);

/*
 * Освободить блок памяти (коды ALLOC_*).
 * ptr — тот же адрес, который раньше вернул allocator_alloc.
 */
int allocator_free(void *ptr);

/* Получить статистику аллокатора (снимок под спинлоком). */
struct stats_info allocator_get_stats(void);

/* Очистить аллокатор и освободить память ядра (bitmap и пул). */
void allocator_cleanup(void);

/*
 * Сформировать строку о состоянии bitmap для параметра bitmap_info.
 * buf    — буфер sysfs, куда писать текст;
 * buflen — размер буфера в байтах (обычно PAGE_SIZE), чтобы не выйти за границу.
 */
int allocator_format_bitmap_info(char *buf, size_t buflen);

#endif /* _KERNEL_ALLOCATOR_H */
