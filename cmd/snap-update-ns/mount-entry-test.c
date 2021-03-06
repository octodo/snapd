/*
 * Copyright (C) 2017 Canonical Ltd
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 3 as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 *
 */

#include "mount-entry.h"
#include "mount-entry.c"

#include <stdarg.h>

#include <glib.h>

#include "test-utils.h"
#include "test-data.h"

static void test_sc_load_mount_profile()
{
	struct sc_mount_entry_list *fstab
	    __attribute__ ((cleanup(sc_cleanup_mount_entry_list))) = NULL;
	struct sc_mount_entry *entry;
	sc_test_write_lines("test.fstab", test_entry_str_1, test_entry_str_2,
			    NULL);
	fstab = sc_load_mount_profile("test.fstab");
	g_assert_nonnull(fstab);

	entry = fstab->first;
	test_looks_like_test_entry_1(entry);
	g_assert_nonnull(entry->next);

	entry = entry->next;
	test_looks_like_test_entry_2(entry);
	g_assert_null(entry->next);

	entry = fstab->last;
	test_looks_like_test_entry_2(entry);
	g_assert_nonnull(entry->prev);

	entry = entry->prev;
	test_looks_like_test_entry_1(entry);
	g_assert_null(entry->prev);
}

static void test_sc_load_mount_profile__no_such_file()
{
	struct sc_mount_entry_list *fstab
	    __attribute__ ((cleanup(sc_cleanup_mount_entry_list))) = NULL;

	fstab = sc_load_mount_profile("test.does-not-exist.fstab");
	g_assert_nonnull(fstab);
	g_assert_null(fstab->first);
	g_assert_null(fstab->last);
}

static void test_sc_save_mount_profile()
{
	struct sc_mount_entry_list fstab;
	struct sc_mount_entry entry_1 = test_entry_1;
	struct sc_mount_entry entry_2 = test_entry_2;
	fstab.first = &entry_1;
	fstab.last = &entry_2;
	entry_1.prev = NULL;
	entry_1.next = &entry_2;
	entry_2.prev = &entry_1;
	entry_2.next = NULL;

	// We can save the profile defined above.
	sc_save_mount_profile(&fstab, "test.fstab");

	// Cast-away the const qualifier. This just calls unlink and we don't
	// modify the name in any way. This way the signature is compatible with
	// that of GDestroyNotify.
	g_test_queue_destroy((GDestroyNotify) sc_test_remove_file,
			     (char *)"test.fstab");

	// After reading the generated file it looks as expected.
	FILE *f = fopen("test.fstab", "rt");
	g_assert_nonnull(f);
	char *line = NULL;
	size_t n = 0;
	ssize_t num_read;

	num_read = getline(&line, &n, f);
	g_assert_cmpint(num_read, >, -0);
	g_assert_cmpstr(line, ==, "fsname-1 dir-1 type-1 opts-1 1 2\n");

	num_read = getline(&line, &n, f);
	g_assert_cmpint(num_read, >, -0);
	g_assert_cmpstr(line, ==, "fsname-2 dir-2 type-2 opts-2 3 4\n");

	num_read = getline(&line, &n, f);
	g_assert_cmpint(num_read, ==, -1);

	free(line);
	fclose(f);
}

static void test_sc_compare_mount_entry()
{
	// Do trivial comparison checks.
	g_assert_cmpint(sc_compare_mount_entry(&test_entry_1, &test_entry_1),
			==, 0);
	g_assert_cmpint(sc_compare_mount_entry(&test_entry_1, &test_entry_2), <,
			0);
	g_assert_cmpint(sc_compare_mount_entry(&test_entry_2, &test_entry_1), >,
			0);
	g_assert_cmpint(sc_compare_mount_entry(&test_entry_2, &test_entry_2),
			==, 0);

	// Ensure that each field is compared.
	struct sc_mount_entry a = test_entry_1;
	struct sc_mount_entry b = test_entry_1;
	g_assert_cmpint(sc_compare_mount_entry(&a, &b), ==, 0);

	b.entry.mnt_fsname = test_entry_2.entry.mnt_fsname;
	g_assert_cmpint(sc_compare_mount_entry(&a, &b), <, 0);
	b = test_entry_1;

	b.entry.mnt_dir = test_entry_2.entry.mnt_dir;
	g_assert_cmpint(sc_compare_mount_entry(&a, &b), <, 0);
	b = test_entry_1;

	b.entry.mnt_opts = test_entry_2.entry.mnt_opts;
	g_assert_cmpint(sc_compare_mount_entry(&a, &b), <, 0);
	b = test_entry_1;

	b.entry.mnt_freq = test_entry_2.entry.mnt_freq;
	g_assert_cmpint(sc_compare_mount_entry(&a, &b), <, 0);
	b = test_entry_1;

	b.entry.mnt_passno = test_entry_2.entry.mnt_passno;
	g_assert_cmpint(sc_compare_mount_entry(&a, &b), <, 0);
	b = test_entry_1;
}

static void test_sc_clone_mount_entry_from_mntent()
{
	struct sc_mount_entry *entry =
	    sc_clone_mount_entry_from_mntent(&test_mnt_1);
	test_looks_like_test_entry_1(entry);
	g_assert_null(entry->next);

	struct sc_mount_entry *next = sc_get_next_and_free_mount_entry(entry);
	g_assert_null(next);
}

static void test_sc_sort_mount_entry_list()
{
	struct sc_mount_entry_list list;

	// Sort an empty list, it should not blow up.
	list.first = NULL;
	list.last = NULL;
	sc_sort_mount_entry_list(&list);
	g_assert(list.first == NULL);
	g_assert(list.last == NULL);

	// Create a list with two items in wrong order (backwards).
	struct sc_mount_entry entry_1 = test_entry_1;
	struct sc_mount_entry entry_2 = test_entry_2;
	list.first = &entry_2;
	list.last = &entry_1;
	entry_2.prev = NULL;
	entry_2.next = &entry_1;
	entry_1.prev = &entry_2;
	entry_1.next = NULL;

	// Sort the list
	sc_sort_mount_entry_list(&list);

	// Ensure that the linkage now follows the right order.
	g_assert(list.first == &entry_1);
	g_assert(list.last == &entry_2);
	g_assert(entry_1.prev == NULL);
	g_assert(entry_1.next == &entry_2);
	g_assert(entry_2.prev == &entry_1);
	g_assert(entry_2.next == NULL);
}

static void __attribute__ ((constructor)) init()
{
	g_test_add_func("/mount-entry/sc_load_mount_profile",
			test_sc_load_mount_profile);
	g_test_add_func("/mount-entry/sc_load_mount_profile/no_such_file",
			test_sc_load_mount_profile__no_such_file);
	g_test_add_func("/mount-entry/sc_save_mount_profile",
			test_sc_save_mount_profile);
	g_test_add_func("/mount-entry/sc_compare_mount_entry",
			test_sc_compare_mount_entry);
	g_test_add_func("/mount-entry/test_sc_clone_mount_entry_from_mntent",
			test_sc_clone_mount_entry_from_mntent);
	g_test_add_func("/mount-entry/test_sort_mount_entries",
			test_sc_sort_mount_entry_list);
}
