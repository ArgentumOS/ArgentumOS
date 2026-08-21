/*
 * Fiwix64 mmap / MAP_SHARED / SysV IPC test (static musl i386).
 *
 * Exercises: anonymous mmap, file mmap with a non-zero offset (mmap2 pgoff),
 * MAP_SHARED across fork, and SysV shared memory (shmget/shmat/shmdt).
 * Writes PASS/FAIL lines to /test.log (persisted by the shutdown flush),
 * then exits, which drives the "last user process" -> Safe to Power Off path.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/mman.h>
#include <sys/wait.h>
#include <sys/shm.h>
#include <sys/sem.h>
#include <sys/msg.h>
#include <sys/ipc.h>
#include <fcntl.h>

static FILE *logf;

static void report(const char *name, int ok)
{
	fprintf(logf, "%s: %s\n", name, ok ? "PASS" : "FAIL");
	fflush(logf);
}

static void checkpoint(void)
{
	sync();   /* flush /test.log so a later hang doesn't hide earlier results */
}

int main(void)
{
	logf = fopen("/test.log", "w");
	if (!logf) {
		logf = stderr;
	}

	/* 1. anonymous mmap */
	{
		char *p = mmap(0, 8192, PROT_READ | PROT_WRITE,
			       MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
		report("anon mmap", p != MAP_FAILED);
		if (p != MAP_FAILED) {
			strcpy(p, "anon-data");
			report("anon write/read", strcmp(p, "anon-data") == 0);
			report("anon second page zero", p[4096] == 0);
			munmap(p, 8192);
		}
	}

	/* 2. file mmap (MAP_SHARED) at a non-zero offset (mmap2 pgoff) */
	{
		int fd = open("/mmap_test.txt", O_CREAT | O_RDWR, 0644);
		report("open /mmap_test.txt", fd >= 0);
		if (fd >= 0) {
			char buf[8192];
			memset(buf, 'A', sizeof(buf));
			report("ftruncate 8192", ftruncate(fd, 8192) == 0);
			write(fd, buf, sizeof(buf));
			lseek(fd, 0, SEEK_SET);
			char *p = mmap(0, 4096, PROT_READ | PROT_WRITE,
				       MAP_SHARED, fd, 4096);
			report("file mmap @offset4096", p != MAP_FAILED);
			if (p != MAP_FAILED) {
				report("file mmap sees 'A'", p[0] == 'A');
				p[0] = 'Z';
				report("file mmap write/read", p[0] == 'Z');
				munmap(p, 4096);
			}
			close(fd);
		}
	}

	/* 3. MAP_SHARED across fork via a file mapping: child's write must be
	 * visible to the parent (Fiwix rejects anonymous MAP_SHARED) */
	{
		int fd = open("/mmap_test.txt", O_RDWR);
		report("open file for shared fork", fd >= 0);
		if (fd >= 0) {
			char *p = mmap(0, 4096, PROT_READ | PROT_WRITE,
				       MAP_SHARED, fd, 0);
			report("shared file mmap", p != MAP_FAILED);
			if (p != MAP_FAILED) {
				strcpy(p, "before");
				pid_t pid = fork();
				if (pid == 0) {
					strcpy(p, "child-wrote");
					_exit(0);
				}
				int st;
				waitpid(pid, &st, 0);
				report("fork MAP_SHARED write visible", strcmp(p, "child-wrote") == 0);
				munmap(p, 4096);
			}
			close(fd);
		}
	}
	checkpoint();

	/* 4. SysV shared memory */
	{
		int shmid = shmget(IPC_PRIVATE, 4096, IPC_CREAT | 0666);
		report("shmget", shmid >= 0);
		if (shmid >= 0) {
			void *p = shmat(shmid, NULL, 0);
			report("shmat", p != (void *)-1);
			if (p != (void *)-1) {
				strcpy((char *)p, "sysv-shm");
				report("shm write/read", strcmp((char *)p, "sysv-shm") == 0);
				shmdt(p);
			}
			shmctl(shmid, IPC_RMID, NULL);
		}
	}

	/* 5. SysV semaphores */
	{
		int semid = semget(IPC_PRIVATE, 1, IPC_CREAT | 0666);
		report("semget", semid >= 0);
		if (semid >= 0) {
			/* init the semaphore to 1, then lock+unlock it */
			if (semctl(semid, 0, SETVAL, 1) == 0) {
				struct sembuf op = { 0, -1, 0 };
				report("semop lock", semop(semid, &op, 1) == 0);
				op.sem_op = 1;
				report("semop unlock", semop(semid, &op, 1) == 0);
				report("semctl GETVAL", semctl(semid, 0, GETVAL) == 1);
			} else {
				report("semctl SETVAL", 0);
			}
			semctl(semid, 0, IPC_RMID);
		}
	}
	checkpoint();

	/* 6. SysV message queues (msgrcv's msgtyp rides the 6th syscall arg) */
	{
		int qid = msgget(IPC_PRIVATE, IPC_CREAT | 0666);
		report("msgget", qid >= 0);
		if (qid >= 0) {
			struct { long mtype; char mtext[16]; } m;
			m.mtype = 7;
			strcpy(m.mtext, "msg-hello");
			report("msgsnd", msgsnd(qid, &m, 16, 0) == 0);
			memset(&m, 0, sizeof(m));
			report("msgrcv", msgrcv(qid, &m, 16, 7, 0) == 16);
			report("msgrcv type/body", m.mtype == 7 &&
			       strcmp(m.mtext, "msg-hello") == 0);
			msgctl(qid, IPC_RMID, NULL);
		}
	}

	checkpoint();
	fclose(logf);
	return 0;
}
