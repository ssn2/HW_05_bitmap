/*
 * Реализация операций над bitmap (задание).
 *
 * Память bitmap хранится как unsigned char[]: младший бит внутри байта
 * соответствует младшему номеру блока в этом байте (удобно считать смещения).
 * Алгоритм first-fit в учебной версии простой — перебор стартовой позиции
 * и проверка need битов подряд; для 2560 блоков этого достаточно.
 */

#include <linux/kernel.h>
#include <linux/types.h>

#include "bitmap.h"

/* Номер байта в массиве bm, в котором лежит бит с глобальным индексом bit. */
static inline size_t byte_index(size_t bit)
{
	return bit >> 3;
}

/* bit_mask — единичка в позиции (bit mod 8) внутри байта. bit — глобальный индекс бита. */
static inline unsigned char bit_mask(size_t bit)
{
	return (unsigned char)(1U << (bit & 7));
}

/* bitmap_bytes_for_blocks — сколько байт нужно под nblocks битов. */
size_t bitmap_bytes_for_blocks(size_t nblocks)
{
	/* nblocks — сколько блоков; сдвиг на 3 = деление на 8 с округлением вверх */
	return (nblocks + 7) >> 3;
}

/* bitmap_set_bit — пометить один блок как занятый (бит = 1). bm — карта, bit — индекс. */
void bitmap_set_bit(unsigned char *bm, size_t bit)
{
	bm[byte_index(bit)] |= bit_mask(bit);
}

/* bitmap_clear_bit — сбросить бит (блок свободен). */
void bitmap_clear_bit(unsigned char *bm, size_t bit)
{
	bm[byte_index(bit)] &= (unsigned char)~bit_mask(bit);
}

/* bitmap_test_bit — прочитать один бит: занят ли блок bit. */
int bitmap_test_bit(const unsigned char *bm, size_t bit)
{
	return (bm[byte_index(bit)] & bit_mask(bit)) != 0;
}

/*
 * bitmap_set_range / bitmap_clear_range — то же для цепочки блоков подряд.
 * Реализация через цикл по одному биту — наглядно и достаточно для размера пула.
 */
void bitmap_set_range(unsigned char *bm, size_t start, size_t n)
{
	size_t i; /* смещение внутри окна [start, start+n) */

	for (i = 0; i < n; i++)
		bitmap_set_bit(bm, start + i);
}

void bitmap_clear_range(unsigned char *bm, size_t start, size_t n)
{
	size_t i; /* индекс внутри диапазона из n блоков */

	for (i = 0; i < n; i++)
		bitmap_clear_bit(bm, start + i);
}

/*
 * bitmap_find_first_fit — поиск первого подходящего «окна» из need нулей.
 * Внешний цикл перебирает возможный start; внутренний проверяет, что все
 * need битов начиная с start свободны. Если нашли — записываем start и выходим.
 */
int bitmap_find_first_fit(const unsigned char *bm, size_t total_blocks,
			  size_t need, size_t *out_start)
{
	size_t start; /* кандидат на начало свободного окна из need битов */
	size_t i;     /* проверка внутри окна: все need битов нулевые? */

	if (!need || need > total_blocks || !out_start)
		return -1;

	for (start = 0; start + need <= total_blocks; start++) {
		for (i = 0; i < need; i++) {
			if (bitmap_test_bit(bm, start + i))
				break;
		}
		if (i == need) {
			*out_start = start;
			return 0;
		}
	}
	return -1;
}

/*
 * bitmap_max_free_run — самая длинная цепочка свободных блоков подряд.
 * Идём слева направо: на нуле бите увеличиваем текущую длину, на единице
 * обнуляем. Максимум текущей длины и есть оценка «самой большой дырки».
 */
size_t bitmap_max_free_run(const unsigned char *bm, size_t total_blocks)
{
	size_t b;     /* текущий номер блока / бита при линейном проходе */
	size_t cur; /* длина текущей непрерывной цепочки нулей */
	size_t best; /* максимум cur за весь проход */

	cur = 0;
	best = 0;

	for (b = 0; b < total_blocks; b++) {
		if (!bitmap_test_bit(bm, b)) {
			cur++;
			if (cur > best)
				best = cur;
		} else {
			cur = 0;
		}
	}
	return best;
}

/* bitmap_count_allocated — сколько блоков помечено как занятые (бит 1). */
size_t bitmap_count_allocated(const unsigned char *bm, size_t total_blocks)
{
	size_t b;   /* индекс бита при подсчёте */
	size_t cnt; /* накопленное число единиц (занятых блоков) */

	cnt = 0;

	for (b = 0; b < total_blocks; b++) {
		if (bitmap_test_bit(bm, b))
			cnt++;
	}
	return cnt;
}
