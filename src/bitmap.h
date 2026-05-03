#ifndef _BITMAP_H
#define _BITMAP_H

/*
 * Вспомогательные функции работы с bitmap (задание, раздел про bitmap).
 *
 * Bitmap — массив unsigned char: в каждом байте 8 бит, бит i отвечает за блок i.
 * Значение 0 — блок свободен, 1 — занят. Здесь только примитивы чтения/записи
 * и поиск first-fit; сам аллокатор вызывает эти функции из allocator.c.
 */

#include <linux/types.h>

/*
 * Сколько байт нужно под nblocks битов (округление вверх до целого байта).
 * nblocks — число блоков пула (= число бит в карте).
 */
size_t bitmap_bytes_for_blocks(size_t nblocks);

/*
 * Установить или сбросить один бит.
 * bm  — массив байт bitmap;
 * bit — глобальный индекс бита (0 … total_blocks-1 в аллокаторе).
 */
void bitmap_set_bit(unsigned char *bm, size_t bit);
void bitmap_clear_bit(unsigned char *bm, size_t bit);

/*
 * Прочитать один бит.
 * bm  — bitmap; bit — индекс. Возврат: ненольно, если блок занят (бит 1).
 */
int bitmap_test_bit(const unsigned char *bm, size_t bit);

/*
 * Пометить или очистить n бит подряд, начиная с индекса start.
 */
void bitmap_set_range(unsigned char *bm, size_t start, size_t n);
void bitmap_clear_range(unsigned char *bm, size_t start, size_t n);

/*
 * First-fit: найти первый индекс, с которого подряд need нулевых битов.
 * bm           — bitmap;
 * total_blocks — сколько бит в карте считаем значимыми;
 * need         — сколько подряд нулей нужно;
 * out_start    — сюда пишем индекс начала окна при успехе.
 * Возврат 0 — нашли, -1 — нет места или плохие аргументы.
 */
int bitmap_find_first_fit(const unsigned char *bm, size_t total_blocks,
			  size_t need, size_t *out_start);

/*
 * Длина самой длинной цепочки свободных (нулевых) битов подряд.
 * bm, total_blocks — как в аллокаторе.
 */
size_t bitmap_max_free_run(const unsigned char *bm, size_t total_blocks);

/*
 * Сколько единичных битов (занятых блоков) в первых total_blocks битах карты.
 */
size_t bitmap_count_allocated(const unsigned char *bm, size_t total_blocks);

#endif /* _BITMAP_H */
