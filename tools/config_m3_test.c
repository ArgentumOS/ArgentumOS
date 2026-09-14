/* config_m3_test.c — M3 probe: the musl identity APIs are served from
 * the FNX .conf domains (system.passwd.conf /
 * system.group.conf in /System/Configuration) instead of legacy
 * colon files (docs/design/system-config-files-plan.md M3).
 *
 * getpwnam/getpwuid/getgrnam/getgrgid/getpwent/getgrent/getgrouplist
 * must all resolve the shipped records (uid 0 Admin, group gid 0).
 * Prints PASS/FAIL per check; exit status = number of failures. Run
 * in the guest (the reader uses absolute /System/Configuration paths).
 */
#include <errno.h>
#include <grp.h>
#include <pwd.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static int fails;

static void check(int cond, const char *what)
{
	if(cond) {
		printf("PASS: %s\n", what);
	} else {
		printf("FAIL: %s\n", what);
		fails++;
	}
}

static void check_pw(const struct passwd *pw)
{
	check(pw && pw->pw_name && !strcmp(pw->pw_name, "Admin"),
	      "getpwnam(Admin) record name");
	check(pw && pw->pw_uid == 0, "getpwnam(Admin) uid == 0");
	check(pw && pw->pw_gid == 0, "getpwnam(Admin) gid == 0");
	check(pw && pw->pw_gecos && !strcmp(pw->pw_gecos, "Admin"),
	      "getpwnam(Admin) gecos");
	check(pw && pw->pw_dir && !strcmp(pw->pw_dir, "/Users/Admin"),
	      "getpwnam(Admin) home");
	check(pw && pw->pw_shell &&
	      !strcmp(pw->pw_shell, "/System/Tools/sh"),
	      "getpwnam(Admin) shell");
}

int main(void)
{
	struct passwd *pw;
	struct group *gr;
	struct passwd *pwent;
	struct group *grent;
	gid_t groups[8];
	int ngroups = 8, ret;
	int n = 0;

	errno = 0;
	pw = getpwnam("Admin");
	check(pw != NULL, "getpwnam(Admin) found");
	check_pw(pw);
	check(errno == 0, "getpwnam leaves errno 0");

	pw = getpwuid(0);
	check(pw && !strcmp(pw->pw_name, "Admin"), "getpwuid(0) == Admin");
	check(pw && pw->pw_uid == 0, "getpwuid(0) uid == 0");

	check(getpwnam("nobody") == NULL, "getpwnam(nobody) == NULL");
	check(getpwuid(65534) == NULL, "getpwuid(65534) == NULL");

	gr = getgrnam("Admin");
	check(gr && !strcmp(gr->gr_name, "Admin"), "getgrnam(Admin) name");
	check(gr && gr->gr_gid == 0, "getgrnam(Admin) gid == 0");
	check(gr && gr->gr_mem && !gr->gr_mem[0],
	      "getgrnam(Admin) has no members");

	gr = getgrgid(0);
	check(gr && !strcmp(gr->gr_name, "Admin"), "getgrgid(0) == Admin");
	check(getgrnam("root") == NULL, "getgrnam(root) == NULL");

	/* sequential passwd iteration (uid-sorted) */
	setpwent();
	while((pwent = getpwent())) {
		n++;
		check_pw(pwent);
	}
	endpwent();
	check(n == 1, "getpwent yields exactly the admin record");

	/* sequential group iteration (gid-sorted) */
	n = 0;
	setgrent();
	while((grent = getgrent())) {
		n++;
		if(grent->gr_gid == 0) {
			check(!strcmp(grent->gr_name, "Admin"),
			      "getgrent first record admin gid 0");
		}
	}
	endgrent();
	check(n == 1, "getgrent yields exactly the admin group");

	/* supplementary-group resolution from the domain */
	ret = getgrouplist("Admin", 0, groups, &ngroups);
	check(ret != -1 && ngroups == 1 && groups[0] == 0,
	      "getgrouplist(admin, 0) returns gid 0");

	if(fails) {
		printf("%d FAILURE(S)\n", fails);
	} else {
		printf("ALL PASS\n");
	}
	return fails;
}
