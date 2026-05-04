#!/usr/bin/env bash
#
# Сценарий тестирования:
# 1) несколько выделений разного размера;
# 2) проверка статистики;
# 3) освобождение в произвольном порядке (не как приходило);
# 4) снова статистика;
# плюс граничные случаи: слишком большой alloc, 0 байт, повторный free.
#

set -euo pipefail

MOD="kernel_alloc"
KO="${KO:-$(dirname "$0")/../build/${MOD}.ko}"
SYS="/sys/module/${MOD}/parameters"

if [[ ! -f "$KO" ]]; then
    echo "Сначала соберите модуль: make kbuild (ожидается $KO)" >&2
    exit 1
fi

if [[ $(id -u) -ne 0 ]]; then
    echo "Запустите от root: sudo $0" >&2
    exit 1
fi

cleanup() {
    rmmod "$MOD" 2>/dev/null || true
}
trap cleanup EXIT

insmod "$KO"

# Очищаем кольцевой буфер, чтобы парсить только адреса этого прогона.
if ! dmesg -C 2>/dev/null; then
    echo "Предупреждение: dmesg -C недоступен, разбор адресов может захватить старые строки" >&2
fi

echo "=== 1) Статистика в начале (весь пул свободен) ==="
cat "$SYS/stats"

echo "=== 2) Несколько выделений разного размера (байты → блоки по 4 KiB) ==="
# 4096 — 1 блок; 12288 — 3 блока; 8192 — 2 блока (разные длины для проверки учёта).
echo 4096 >"$SYS/alloc"
echo 12288 >"$SYS/alloc"
echo 8192 >"$SYS/alloc"

echo "=== 3) Статистика после трёх alloc (должно совпасть с суммой блоков: 1+3+2 = 6 блоков = 24 KiB) ==="
cat "$SYS/stats"

echo "=== 4) Фрагмент bitmap_info ==="
head -c 160 "$SYS/bitmap_info"
echo

# Собираем три последних адреса выделения в порядке появления в журнале.
mapfile -t ADDRS < <(dmesg | grep "${MOD}:" | grep "allocated" | sed -n 's/.* at \([0-9a-fA-F]*\).*/0x\1/p')

dmesg | grep "${MOD}:" | grep "allocated"
# | sed -n 's/.* at \(0x[0-9a-fA-F]*\).*/\1/p'

if [[ ${#ADDRS[@]} -lt 3 ]]; then
    echo "Ожидалось не меньше трёх строк «allocated» в dmesg, получено: ${#ADDRS[@]}" >&2
#    dmesg | tail -20 >&2
    exit 1
fi

A0="${ADDRS[0]}"
A1="${ADDRS[1]}"
A2="${ADDRS[2]}"
echo "Адреса выделений (по порядку alloc): $A0, $A1, $A2"

echo "=== 5) Освобождение не в порядке LIFO: сначала второй, потом первый, потом третий ==="
echo "${SYS}/free"
echo "${A1}" >"${SYS}/free"
echo "${A0}" >"${SYS}/free"
echo "${A2}" >"${SYS}/free"

echo "=== 6) Статистика после free (выделено 0 KiB, весь пул снова свободен) ==="
cat "$SYS/stats"

echo "=== 7) Повторный free по тому же адресу (ожидаем ошибку, запись не должна пройти) ==="
set +e
echo "$A0" >"$SYS/free"
EC_DUP=$?
set -e
if [[ $EC_DUP -eq 0 ]]; then
    echo "Ошибка: повторный free должен был завершиться с ошибкой" >&2
    exit 1
fi

echo "=== 8) alloc больше пула (ожидаем ошибку записи в sysfs) ==="
set +e
echo $((11 * 1024 * 1024)) >"$SYS/alloc"
EC_BIG=$?
set -e
if [[ $EC_BIG -eq 0 ]]; then
    echo "Ожидалась ошибка при alloc > 10 MiB" >&2
    exit 1
fi

echo "=== 9) alloc 0 байт (неверный размер по заданию) ==="
set +e
echo 0 >"$SYS/alloc"
EC_ZERO=$?
set -e
if [[ $EC_ZERO -eq 0 ]]; then
    echo "Ожидалась ошибка для 0 байт" >&2
    exit 1
fi

echo "OK: Тести пройдены успешно"
