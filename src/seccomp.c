/*
 * Copyright (C) 2015 Canonical Ltd
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
#include <errno.h>
#include <stdio.h>
#include <unistd.h>
#include <string.h>
#include <ctype.h>
#include <stdlib.h>
#include <sys/prctl.h>

#include <seccomp.h>

#include "utils.h"

char *filter_profile_dir = "/var/lib/snappy/seccomp/profiles/";

// strip whitespace from the end of the given string (inplace)
size_t trim_right(char *s, size_t slen) {
   while(slen > 0 && isspace(s[slen - 1])) {
      s[--slen] = 0;
   }
   return slen;
}

int seccomp_load_filters(const char *filter_profile)
{
   debug("seccomp_load_filters %s", filter_profile);
   int rc = 0;
   int syscall_nr = -1;
   scmp_filter_ctx ctx = NULL;
   FILE *f = NULL;
   size_t lineno = 0;

   ctx = seccomp_init(SCMP_ACT_KILL);
   if (ctx == NULL)
      return ENOMEM;

   // Disable NO_NEW_PRIVS because it interferes with exec transitions in
   // AppArmor. Unfortunately this means that security policies must be very
   // careful to not allow the following otherwise apps can escape the snadbox:
   //   - seccomp syscall
   //   - prctl with PR_SET_SECCOMP
   //   - ptrace (trace) in AppArmor
   //   - capability sys_admin in AppArmor
   // Note that with NO_NEW_PRIVS disabled, CAP_SYS_ADMIN is required to change
   // the seccomp sandbox.
   if (getenv("UBUNTU_CORE_LAUNCHER_NO_ROOT") == NULL) {
      rc = seccomp_attr_set(ctx, SCMP_FLTATR_CTL_NNP, 0);
      if (rc != 0) {
         fprintf(stderr, "Cannot disable nnp\n");
         return -1;
      }
   }

   if (getenv("SNAPPY_LAUNCHER_SECCOMP_PROFILE_DIR") != NULL)
      filter_profile_dir = getenv("SNAPPY_LAUNCHER_SECCOMP_PROFILE_DIR");

   char profile_path[128];
   if (snprintf(profile_path, sizeof(profile_path), "%s/%s", filter_profile_dir, filter_profile) < 0) {
      goto out;
   }

   f = fopen(profile_path, "r");
   if (f == NULL) {
      fprintf(stderr, "Can not open %s (%s)\n", profile_path, strerror(errno));
      return -1;
   }
   // 80 characters + '\n' + '\0'
   char buf[82];
   while (fgets(buf, sizeof(buf), f) != NULL)
   {
      size_t len;

      lineno++;

      // comment, ignore
      if(buf[0] == '#')
         continue;

      // ensure the entire line was read
      len = strlen(buf);
      if (len == 0)
         continue;
      else if (buf[len - 1] != '\n' && len > (sizeof(buf) - 2)) {
         fprintf(stderr, "seccomp filter line %zu was too long (%zu characters max)\n", lineno, sizeof(buf) - 2);
         rc = -1;
         goto out;
      }

      // kill final newline
      len = trim_right(buf, len);
      if (len == 0)
         continue;

      // check for special "@unrestricted" command
      if (strncmp(buf, "@unrestricted", sizeof(buf)) == 0)
         goto out;

      // syscall not available on this arch/kernel
      // as this is a syscall whitelist its ok and the error can be ignored
      syscall_nr = seccomp_syscall_resolve_name(buf);
      if (syscall_nr == __NR_SCMP_ERROR)
         continue;

      // a normal line with a syscall
      rc = seccomp_rule_add_exact(ctx, SCMP_ACT_ALLOW, syscall_nr, 0);
      if (rc != 0) {
         rc = seccomp_rule_add(ctx, SCMP_ACT_ALLOW, syscall_nr, 0);
	 if (rc != 0) {
             fprintf(stderr, "seccomp_rule_add failed with %i for '%s'\n", rc, buf);
             goto out;
	 }
      }
   }

   // raise privileges to load seccomp policy since we don't have nnp
   if (getenv("UBUNTU_CORE_LAUNCHER_NO_ROOT") == NULL) {
      if (seteuid(0) != 0)
         die("seteuid failed");
      if (geteuid() != 0)
         die("raising privs before seccomp_load did not work");
   }

   // load it into the kernel
   rc = seccomp_load(ctx);

   if (rc != 0) {
      fprintf(stderr, "seccomp_load failed with %i\n", rc);
      goto out;
   }

 out:
   // drop privileges again
   if (geteuid() == 0) {
      unsigned real_uid = getuid();
      if (seteuid(real_uid) != 0)
         die("seteuid failed");
      if (real_uid != 0 && geteuid() == 0)
         die("dropping privs after seccomp_load did not work");
   }

   if (f != NULL) {
      fclose(f);
   }
   seccomp_release(ctx);
   return rc;
}