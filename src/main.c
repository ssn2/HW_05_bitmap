/*
 * Точка входа модуля kernel_alloc.
 *
 * При загрузке модуля один раз вызывается allocator_init(): выделяются bitmap
 * и пул. При выгрузке — allocator_cleanup(): память возвращается ядру.
 * Взаимодействие с пользователем через module_param_cb реализовано в params.c.
 */

#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

#include <linux/kernel.h>
#include <linux/module.h>

#include "allocator.h"

/* kernel_alloc_init — загрузка модуля: инициализация аллокатора. */
static int __init kernel_alloc_init(void)
{
	if (allocator_init() != 0) {
		pr_err("allocator_init failed\n");
		return -ENOMEM;
	}

	pr_info("loaded (pool %u MiB, block %u bytes, %u blocks)\n",
		(unsigned int)(ALLOCATOR_TOTAL_MEMORY / (1024U * 1024U)),
		(unsigned int)ALLOCATOR_BLOCK_SIZE,
		(unsigned int)ALLOCATOR_TOTAL_BLOCKS);
	return 0;
}

/* kernel_alloc_exit — выгрузка модуля: освобождение ресурсов аллокатора. */
static void __exit kernel_alloc_exit(void)
{
	allocator_cleanup();
	pr_info("unloaded\n");
}

module_init(kernel_alloc_init);
module_exit(kernel_alloc_exit);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Smolovoy Sergey");
MODULE_DESCRIPTION("Bitmap memory allocator via module_param_cb");
