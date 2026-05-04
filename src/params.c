/*
 * Интерфейс module_param_cb.
 *
 * Параметры модуля в sysfs позволяют из пользовательского пространства
 * выделять память (alloc), освобождать по числовому адресу (free), читать
 * статистику (stats) и превью bitmap (bitmap_info). Все колбэки должны
 * проверять вход: неверная строка числа, ноль байт на alloc и т.д.
 */

#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/moduleparam.h>
#include <linux/stat.h>
#include <linux/string.h>

#include "allocator.h"

/*
 * param_set_alloc — запись в параметр alloc.
 * val — сырая строка из sysfs (число байт);
 * kp  — описатель параметра ядра (здесь не используем, но аргумент обязателен).
 * Пользователь передаёт размер в байтах (например echo 8192 > .../alloc).
 * Ноль байт отклоняем; при успехе адрес нового блока виден в журнале ядра (dmesg).
 */
static int param_set_alloc(const char *val, const struct kernel_param *kp)
{
	unsigned long bytes; /* размер запроса после разбора строки из sysfs */
	void *p;             /* указатель на выделенный регион или NULL */

	(void)kp; /* не используем — сигнатура module_param_ops обязательна */

	if (kstrtoul(val, 0, &bytes))
		return -EINVAL;

	if (bytes == 0)
		return -EINVAL;

	p = allocator_alloc((size_t)bytes);
	if (!p) {
		pr_warn("alloc %lu bytes failed\n", bytes);
		return -ENOMEM;
	}
	pr_info("allocated %lu bytes at %lx\n", bytes, (unsigned long)p);
	return 0;
}

/* param_ops_alloc — связка имени параметра alloc с колбэком записи. */
static const struct kernel_param_ops param_ops_alloc = {
	.set = param_set_alloc,
};

module_param_cb(alloc, &param_ops_alloc, NULL, 0200);
MODULE_PARM_DESC(alloc,
		 "Allocate bytes (e.g. echo 8192 > .../alloc); address in dmesg");

/*
 * param_set_free — запись в параметр free.
 * val — строка с адресом в виде числа; kp — как в param_set_alloc, не трогаем.
 * Адрес передаётся как число (десятичное или 0x...), приводится к указателю.
 * Коды allocator_free мапятся на errno для sysfs.
 */
static int param_set_free(const char *val, const struct kernel_param *kp)
{
	unsigned long addr; /* числовой адрес из строки (dec или hex) */
	void *ptr;          /* тот же битовый образ, что и addr, для allocator_free */
	int ret;            /* код ALLOC_* от аллокатора */

	(void)kp;

	if (kstrtoul(val, 0, &addr))
		return -EINVAL;

	ptr = (void *)addr;
	ret = allocator_free(ptr);
	switch (ret) {
	case ALLOC_OK:
		pr_info("freed memory at %lx\n", (unsigned long)ptr);
		return 0;
	case ALLOC_INVALID:
		return -EINVAL;
	case ALLOC_NOT_FOUND:
		pr_warn("free: pointer not found %lx\n", (unsigned long)ptr);
		return -ENOENT;
	default:
		return -EIO;
	}
}

/* param_ops_free — параметр free и функция освобождения по адресу. */
static const struct kernel_param_ops param_ops_free = {
	.set = param_set_free,
};

module_param_cb(free, &param_ops_free, NULL, 0200);
MODULE_PARM_DESC(free,
		 "Free by address (e.g. echo 0xffff888012340000 > .../free)");

/*
 * param_get_stats — чтение параметра stats.
 * buf — буфер sysfs, куда положить текст; kp не используется.
 * Возвращаем одну строку: объёмы в KiB и процент фрагментации, как в примере задания.
 */
static int param_get_stats(char *buf, const struct kernel_param *kp)
{
	struct stats_info s; /* снимок счётчиков из аллокатора */

	(void)kp;

	s = allocator_get_stats();
	return scnprintf(
		buf, PAGE_SIZE,
		"Total: %zu KB | Free: %zu KB | Allocated: %zu KB | Fragmentation: %zu%%\n",
		s.total_memory / 1024U, s.free_memory / 1024U,
		s.allocated_memory / 1024U, s.fragmentation_percent);
}

/* param_ops_stats — только чтение: отдаём строку статистики. */
static const struct kernel_param_ops param_ops_stats = {
	.get = param_get_stats,
};

module_param_cb(stats, &param_ops_stats, NULL, 0444);
MODULE_PARM_DESC(stats, "Allocator statistics (read-only)");

/*
 * param_get_bitmap_info — чтение параметра bitmap_info.
 * buf — страница sysfs; kp не используется.
 * Текст формирует allocator_format_bitmap_info() в allocator.c.
 */
static int param_get_bitmap_info(char *buf, const struct kernel_param *kp)
{
	(void)kp;

	return allocator_format_bitmap_info(buf, PAGE_SIZE);
}

/* param_ops_bitmap_info — только чтение: превью bitmap в тексте. */
static const struct kernel_param_ops param_ops_bitmap_info = {
	.get = param_get_bitmap_info,
};

module_param_cb(bitmap_info, &param_ops_bitmap_info, NULL, 0444);
MODULE_PARM_DESC(bitmap_info, "Bitmap preview (read-only)");
