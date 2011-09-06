#include <stdio.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <fnmatch.h>
#include <dirent.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <pwd.h>
#include <grp.h>
#include <unistd.h>

// determine the max path size
#ifndef MAX_PATH
# ifdef _POSIX_PATH_MAX
#  define MAX_PATH _POSIX_PATH_MAX
# else
#  define MAX_PATH 1024
# endif
#endif

// condition type:
#define COND_NAME 1
#define COND_TYPE 2

// command:
#define CMD_PRINT 1
#define CMD_LS    2

// structure for holding conditions (-name, -type, etc..)
typedef struct {
	char *data;
	int reverse;
	int type;
}
condition;

// simple list structure
typedef struct {
	void **nodes;
	int node_size;
	int length;
}
list;

// adds new node to the list
void push_l (list *l, void *node) {
	l->nodes = (void **) realloc ((void **)l->nodes, l->node_size * (l->length+1));
	l->nodes[l->length++] = node;
}

// adds new condition to the conditions list
condition * new_condition (char *data, int reverse, int type) {
	condition *cond = (condition *) malloc (sizeof(condition));
	cond->data = data;
	cond->reverse = reverse;
	cond->type = type;
	return cond;
}

// 'calculate' the permissions string
char * calc_perm_str (struct stat *s) {
	static char perm_str[10];
	
	sprintf (perm_str, "----------");
	
	// calculate file type:
	if (S_ISDIR(s->st_mode)) {
		perm_str[0] = 'd';
	} else if (S_ISCHR(s->st_mode)) {
		perm_str[0] = 'c';
	} else if (S_ISBLK(s->st_mode)) {
		perm_str[0] = 'b';
	} else if (S_ISLNK(s->st_mode)) {
		perm_str[0] = 'l';
	} else if (S_ISFIFO(s->st_mode)) {
		perm_str[0] = 'p';
	} else if (S_ISSOCK(s->st_mode)) {
		perm_str[0] = 's';
	}

	// calculate permissions:
	if (s->st_mode & S_IRUSR) {
		perm_str[1] = 'r';
	}
	if (s->st_mode & S_IWUSR) {
		perm_str[2] = 'w';
	}
	if (s->st_mode & S_IXUSR) {
		perm_str[3] = 'x';
	}
	if (s->st_mode & S_IRGRP) {
		perm_str[4] = 'r';
	}
	if (s->st_mode & S_IWGRP) {
		perm_str[5] = 'w';
	}
	if (s->st_mode & S_IXGRP) {
		perm_str[6] = 'x';
	}
	if (s->st_mode & S_IROTH) {
		perm_str[7] = 'r';
	}
	if (s->st_mode & S_IWOTH) {
		perm_str[8] = 'w';
	}
	if (s->st_mode & S_IXOTH) {
		perm_str[9] = 'x';
	}

	return perm_str;
}

// returns the date string, confirming to the yyyy-dd-mm HH:MM format
char * calc_date_str (time_t time) {
	static char date_str[25];
	struct tm *t;

	t = localtime (&time);
	if (!t) {
		perror ("localtime");
		exit (-1);
	}
	strftime (date_str, 25, "%Y-%d-%m %H:%M", t);
	return date_str;
}

// returns the link contents
char * calc_link_dest (const char *path) {
	static char link_dest[MAX_PATH];

	if (readlink (path, link_dest, MAX_PATH) == -1) {
		perror ("readlink");
		exit (-1);
	}
	return link_dest;
}

// returns the basename of the path
char * calc_basename (const char *path) {
	static char tmp[MAX_PATH];
	int i;
	tmp[0] = '\0';
	strcpy (tmp, path);
	// strip last backslashes
	for (i = strlen(tmp); i > 0 && tmp[i] == '/'; --i) {
		tmp[i] = '\0';
	}
	// find last backslash
	for (; i > 0 && tmp[i] != '/'; --i);
	return (tmp+i+1);
}

// recursive find function
// arguments:
// path - the path to start searching from
// cond_l - list of conditions that file must match (-name, -type, etc...)
// cmd - command to execute on found file (-print, -ls, etc...)
void find_r (const char *path, list *cond_l, int cmd) {
	int i, cond_true;
	char *nextpath;
	DIR *dirp;
	struct dirent *dir;
	struct stat f_stat;
	struct passwd *pw;
	struct group *gr;

	if (lstat (path, &f_stat) == -1) {
		perror (path);
		return; // continue to the next file
	}

        // if list of conditions is empty - this file will be printed
	cond_true = (cond_l->length == 0);

	for (i=0; i<cond_l->length; i++) {
		condition *cond = cond_l->nodes[i];
		// check the current condition
		switch (cond->type) {
			case COND_NAME:
				cond_true = (fnmatch (cond->data, calc_basename(path), 0) == 0);
				break;
			case COND_TYPE:
				switch (*(cond->data)) {
					case 'f':
						cond_true = S_ISREG(f_stat.st_mode);
						break;
					case 'p':
						cond_true = S_ISFIFO(f_stat.st_mode);
						break;
					case 'b':
						cond_true = S_ISBLK(f_stat.st_mode);
						break;
					case 'c':
						cond_true = S_ISCHR(f_stat.st_mode);
						break;
					case 'd':
						cond_true = S_ISDIR(f_stat.st_mode);
						break;
					case 'l':
						cond_true = S_ISLNK(f_stat.st_mode);
						break;
					case 's':
						cond_true = S_ISSOCK(f_stat.st_mode);
						break;
					default:
						fprintf (stderr,
							"-type only accepts 'f','p','b','c','d','s' or 'l'.\n");
						exit (-1);
				}
				break;
		}
		if (cond->reverse) {
			cond_true = !cond_true;
		}
		if (!cond_true) {
			break;
		}
	}

	if (cond_true) {
		// perform the command
		switch (cmd) {
			case CMD_PRINT:
				printf ("%s\n", path);
				break;
			case CMD_LS:
				pw = getpwuid (f_stat.st_uid);
				if (!pw) {
					perror ("getpwuid");
					exit (-1);
				}
				gr = getgrgid (f_stat.st_gid);
				if (!gr) {
					perror ("getgrgid");
					exit (-1);
				}

				printf ("%d %d %s %d %s %s",
					f_stat.st_ino, // inode number
					f_stat.st_size/1024, // size in KB
					calc_perm_str (&f_stat), // permissions
					f_stat.st_nlink, // number of links
					pw->pw_name, // owner's name
					gr->gr_name // group name
				);
				// print the file size on non-block and non-character device files
				if (!S_ISBLK(f_stat.st_mode) && !S_ISCHR(f_stat.st_mode)) {
					printf (" %ld", f_stat.st_size); // size in bytes
				}
				printf (" %s %s",
					calc_date_str (f_stat.st_mtime), // modification date
					path // file path
				);
				if (S_ISLNK(f_stat.st_mode)) {
					printf (" -> %s\n", calc_link_dest(path));
				} else {
					printf ("\n");
				}
				break;
		}
	}

	if (S_ISDIR(f_stat.st_mode)) {
		dirp = opendir (path);
		if (!dirp) {
			perror (path);
			return; // continue to the next file
		}
		while ((dir = readdir (dirp)) != NULL) {
			// skip the current directory and the directory above
			if (!strcmp(dir->d_name, ".") || !strcmp(dir->d_name, "..")) {
				continue;
			}
			// calculate the next path
			nextpath = (char *) malloc (strlen(path) + strlen(dir->d_name) + 2);
			sprintf (nextpath, "%s/%s", path, dir->d_name);

			// recursive call:
			find_r (nextpath, cond_l, cmd);
			free (nextpath);
		}
		closedir (dirp);
	}
}

int main (int argc, char *argv[]) {

	list cond_l;
	list path_l;
	int cmd = CMD_PRINT, i, reverse = 0, arg_path = 1;

	// initialize conditions and paths lists:
	cond_l.nodes = NULL;
	cond_l.length = 0;
	cond_l.node_size = sizeof(condition);
	path_l.nodes = NULL;
	path_l.length = 0;
	path_l.node_size = sizeof(char *);
	
	for (i=1; i<argc; i++) {
		if (!strcmp (argv[i], "!")) {
			arg_path = 0;
			reverse = 1;
		}
		else if (!strcmp (argv[i], "-name")) {
			arg_path = 0;
			push_l (&cond_l, new_condition (argv[i+1], reverse, COND_NAME));
			i++; // skip next argument
			reverse = 0; // fire off reverse flag
		}
		else if (!strcmp (argv[i], "-type")) {
			arg_path = 0;
			push_l (&cond_l, new_condition (argv[i+1], reverse, COND_TYPE));
			i++;
			reverse = 0;
		}
		else if (!strcmp (argv[i], "-print")) {
			cmd = CMD_PRINT;
			break;
		}
		else if (!strcmp (argv[i], "-ls")) {
			cmd = CMD_LS;
			break;
		}
		else if (arg_path) {
			push_l (&path_l, argv[i]);
		}
	}

	// user must give at least one path
	if (path_l.length == 0) {
		fprintf (stderr, "USAGE: %s path1 [ path2 ... path<N> ] expression\n", argv[0]);
		exit (-1);
	}

	// for each provided path, run the find procedure
	for (i=0; i<path_l.length; i++) {
		find_r (path_l.nodes[i], &cond_l, cmd);
	}
}
