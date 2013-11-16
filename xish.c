#include <sys/types.h>
#include <unistd.h>
#include <sys/wait.h>
#include <fcntl.h>

#include <limits.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <ctype.h>

#define STR_SIZE 128
#define PARAM_COUNT 5
#define DFL_PROMPT "$ "
#define SIG_FATALERR SIGUSR1
#define SIG_EXIT SIGUSR2

/* Todo:
 * -New line after CTR-C?
 * -Move checkJobs, so "jobs" could be redirected?
 * -Rework internal command handling
 * -Normal syntax check
 * -Commands and redirectors written together, special character taging!
 */

typedef enum { RET_OK, RET_ERR, RET_EOF } result_t;
typedef enum { ST_NONE, ST_RUNNING, ST_DONE, ST_STOPPED, ST_JUSTSTP } status_t;

typedef struct
{
	char *job;
	int pgid;
	status_t status;
} job_t;

/* Terminates program */
void fatalError ()
{
	putchar ('\n');
	exit (1);
}

/* Reallocates str if it has reached maximum capacity */
void checkStringLen (char **str, int len)
{
	char *ptr;

	if (len % STR_SIZE > 0)
		return;
	if ((ptr = realloc (*str, (len + STR_SIZE) * sizeof (char))) == NULL)
		fatalError ();

	*str = ptr;
}

/* Realocates params if it has reached maximum capacity */
void checkParamCnt (char ***params, int nparams)
{
	char **ptr;

	if (nparams % PARAM_COUNT > 0)
		return;
	if ((ptr = realloc (*params, (nparams + PARAM_COUNT) * sizeof (char *))) == NULL)
		fatalError ();

	*params = ptr;
}

/* Adds null terminator to a string, reallocates it if neccessary */
void endString (char **str, int len)
{
	char *ptr;

	if (len % STR_SIZE == 0)
	{
		if ((ptr = realloc (*str, (len + 1) * sizeof (char))) == NULL)
			fatalError ();
		*str = ptr;
	}

	(*str)[len] = '\0';
}

/* Adds ch to a string */
void addChar (char **str, int *len, char ch)
{
	checkStringLen (str, *len);
	(*str)[(*len)++] = ch;
}

/* Adds one parameter to params */
void addParam (char ***params, int *nparams)
{
	if (*nparams)
		checkParamCnt (params, *nparams + 1);
	if (((*params)[(*nparams)++] = calloc (STR_SIZE, sizeof (char))) == NULL)
		fatalError ();
}

/* Frees params array */
void clearParams (char ***params, int nparams)
{
	int i;

	if (params == NULL)
		return;
	for (i = 0; i < nparams; ++i)
		free ((*params)[i]);
	free (*params);

	*params = NULL;
}

/* Adds new entry to jobs array, initializes the structure with given data */
void addJob (job_t **jobs, int *njobs, char **command, int nparams, int pgid, status_t status)
{
	int len = nparams + 1, i;
	job_t *ptr;

	if ((ptr = realloc (*jobs, (*njobs + 1) * sizeof (job_t))) == NULL)
		fatalError ();

	*jobs = ptr;
	ptr = *jobs + *njobs;
	ptr->pgid = pgid;
	ptr->status = status;

	for (i = 0; i < nparams; ++i)
		len += strlen (command[i]);
	if ((ptr->job = calloc (len + 1, sizeof (char))) == NULL)
		fatalError ();

	len = 0;
	for (i = 0; i < nparams; ++i)
	{
		strcpy (ptr->job + len, command[i]);
		len = strlen (ptr->job);
		ptr->job[len] = ' ';
		ptr->job[++len] = '\0';
	}

	if (status == ST_RUNNING)
		printf ("[%d] %d\n", *njobs + 1, ptr->pgid);
	++(*njobs);
}

/* Deletes done entry form jobs array */
void deleteJob (job_t **jobs, int *njobs, int n) /* CHECK RETURN!!!!!!!!! */
{
	int i;
	job_t *ptr;

	free ((*jobs)[n].job);
	(*jobs)[n].job = NULL;
	(*jobs)[n].status = ST_NONE;
	if (n == *njobs - 1)
	{
		i = n;
		while (((*jobs)[i].status == ST_NONE) && --i >= 0);
		if ((ptr = realloc (*jobs, (i + 1) * sizeof (job_t))) == NULL && i + 1 > 0)
			fatalError ();
		*jobs = ptr;
		*njobs = i + 1;
	}
}

/* Frees jobs array */
void clearJobs (job_t **jobs, int njobs)
{
	int i;

	if (*jobs == NULL)
		return;
	for (i = 0; i < njobs; ++i)
		if ((*jobs)[i].job != NULL)
			free ((*jobs)[i].job);
	free (*jobs);
	*jobs = NULL;
}

/* Show current jobs status */
void showJobs (job_t *jobs, int njobs, int fullog)
{
	int i;
	char status[8];

	for (i = 0; i < njobs; ++i)
	{
		if (jobs[i].status == ST_NONE || (!fullog && (jobs[i].status == ST_RUNNING || jobs[i].status == ST_STOPPED)))
		    continue;
		switch (jobs[i].status)
		{
			case ST_NONE:	 break;
			case ST_DONE: 	 strcpy (status, "Done");	 break;
			case ST_RUNNING: strcpy (status, "Running"); break;
			case ST_JUSTSTP: jobs[i].status = ST_STOPPED;
			case ST_STOPPED: strcpy (status, "Stopped"); break;
		}
		printf ("[%d] %s\t\t%s\n", i + 1, status, jobs[i].job);
	}
}

/* Checks jobs statuses. fullog determines if all the jobs should be shown, or only the done and just stopped ones */
void checkJobs (job_t **jobs, int *njobs, int fullog)
{
	int i, status;
	pid_t pid;
	job_t *job;

	for (i = 0; i < *njobs; ++i)
	{
		job = *jobs + i;
		if (job->status == ST_NONE)
			continue;
		while ((pid = waitpid (-job->pgid, &status, WNOHANG | WUNTRACED | WCONTINUED)) > 0)
			if (job->pgid == pid)
			{
				if (WIFSTOPPED (status))
					job->status = ST_JUSTSTP;
				else if (WIFCONTINUED (status))
					job->status = ST_RUNNING;
				else
					job->status = ST_DONE;
			}
	}
	showJobs (*jobs, *njobs, fullog);
	for (i = 0; i < *njobs; ++i)
		if ((*jobs)[i].status == ST_DONE)
			deleteJob (jobs, njobs, i);
}

/* Reads the infinite string and parses it into substrings array. Return statuses:
 * RET_OK  - command is correct
 * RET_ERR - wrong command format
 * RET_EOF - EOF found
 * RET_MEMORYERR - memory allocation error
 */
result_t readCommand (char ***params, int *nparams)
{
	enum { IN_INITIAL, IN_WORD, IN_BETWEEN, IN_SCREEN, IN_QUOTES, IN_COMMENT, IN_ERROR } state = IN_INITIAL, previous;
	int ch, len = 0;

	*nparams = 0;
	if ((*params = calloc (PARAM_COUNT, sizeof (char *))) == NULL)
		fatalError ();

	while (1)
	{
		ch = getchar ();
		if (ch == EOF && state != IN_INITIAL)
			continue;
		switch (state)
		{
			case IN_INITIAL:
				if (ch == '\n' || ch == '#' || ch == EOF)
				{
					addParam (params, nparams);
					(*params)[0][0] = '\0';
					if (ch == EOF)
						return RET_EOF;
				}

			case IN_BETWEEN:
				if (ch == '\n')
					return RET_OK;
				else if (ch == '\\')
				{
					previous = IN_WORD;
					state = IN_SCREEN;
					addParam (params, nparams);
					len = 0;
				}
				else if (ch == '#')
					state = IN_COMMENT;
				else if (ch == '"')
				{
					state = IN_QUOTES;
					addParam (params, nparams);
					len = 0;
				}
				else if (isspace (ch))
					;
				else
				{
					state = IN_WORD;
					addParam (params, nparams);
					len = 1;
					(*params)[*nparams - 1][0] = ch;
				}
				break;

			case IN_WORD:
				if (ch == '\n')
				{
					endString (*params + *nparams - 1, len);
					return RET_OK;
				}
				else if (ch == '\\')
				{
					previous = IN_WORD;
					state = IN_SCREEN;
				}
				else if (ch == '#')
				{
					state = IN_COMMENT;
					endString (*params + *nparams - 1, len);
				}
				else if (ch == '"')
					state = IN_ERROR;
				else if (isspace (ch))
				{
					state = IN_BETWEEN;
					endString (*params + *nparams - 1, len);
				}
				else
					addChar (*params + *nparams - 1, &len, ch);
				break;

			case IN_SCREEN:
				if (ch == '\n')
					return RET_ERR;
				else if (ch == '\\' || ch == '#' || ch == '"')
				{
					state = previous;
					addChar (*params + *nparams - 1, &len, ch);
				}
				else
					state = IN_ERROR;
				break;

			case IN_QUOTES:
				if (ch == '\n')
					return RET_ERR;
				else if (ch == '\\')
				{
					previous = IN_QUOTES;
					state = IN_SCREEN;
				}
				else if (ch == '"')
				{
					state = IN_WORD;
					endString (*params + *nparams - 1, len);
				}
				else
					addChar (*params + *nparams - 1, &len, ch);
				break;

			case IN_COMMENT:
				if (ch == '\n')
					return RET_OK;
				break;

			case IN_ERROR:
				if (ch == '\n')
					return RET_ERR;
				break;
		}
	}
}

/* Replaces environmental variables with their value */
void placeEnv (char **params, int nparams)
{
	int i;

	for (i = 0; i < nparams; ++i)
	{
		int len = strlen (params[i]), posp = 0, poss = 0, ch;
		char *s;

		if (len < 2 || !strchr (params[i], '$'))
			continue;
		if ((s = calloc (STR_SIZE, sizeof (char))) == NULL)
			fatalError ();

		while (posp < len)
		{
			char *env;
			int start;

			while (posp < len && params[i][posp] != '$')
			{
				checkStringLen (&s, poss);
				s[poss++] = params[i][posp++];
			}
			if (posp == len)
				break;
			start = posp + 1;

			while (++posp < len && isalpha ((int)params[i][posp]));

			ch = params[i][posp];
			params[i][posp] = '\0';
			if ((env = getenv (params[i] + start)) != NULL)
			{
				if ((poss + strlen (env) + 1) / STR_SIZE > poss / STR_SIZE)
				{
					char *ptr;
					if ((ptr = realloc (s, ((poss + strlen (env) + 1) / STR_SIZE + 1) * STR_SIZE * sizeof (char))) == NULL)
						fatalError ();
					s = ptr;
				}
				strcpy (s + poss, env);
				poss += strlen (env);
			}
			params[i][posp] = ch;
		}

		endString (&s, poss);
		free (params[i]);
		params[i] = s;
	}
}

/* Checks if command is internal that must be executed in the main process */
int isInternal (char **command, int nparams)
{
	int i;

	if (strcmp (command[0], "cd") && strcmp (command[0], "exit") && strcmp (command[0], "jobs") && strcmp (command[0], "fg")
	    && strcmp (command[0], "bg"))
		return 0;
	for (i = 1; i < nparams; ++i)
		if (command[i][0] == '|')
			return 0;

	return 1;
}

/* Shifts process group to foreground and waits for it to finish. Returns 1 if the process was stopped and 0 if it has termeniated */
int waitProcessGroup (pid_t pgid, int *status)
{
	int st;

	tcsetpgrp (STDIN_FILENO, pgid);
	kill (-pgid, SIGCONT);
	waitpid (pgid, &st, WUNTRACED);
	tcsetpgrp (STDIN_FILENO, getpid ());

	if (WIFSTOPPED (st))
		putchar ('\n');
	if (status != NULL)
		*status = WEXITSTATUS (st);

	return WIFSTOPPED (st);
}

/* Checks if is an opening/closing bracket, changes cnt accordingly */
void checkBracket (char *s, int *cnt)   		/* When tagging is done, replace this with findDivider */
{
	if (!strcmp (s, "("))
		++(*cnt);
	else if (!strcmp (s, ")"))
		--(*cnt);
}

int launchJobs (char **, int, job_t **, int *);

/* Executes a straightforward command (no pipes, no dividers - just command with parameters) */
void executeCommand (char **command, int nparams, job_t *jobs, int njobs)
{
	if (command[0][0] == '(') /* Launching subshell */
		exit (launchJobs (command + 1, nparams - 2, &jobs, &njobs));

	if (!nparams || command[0][0] == '\0')
		exit (0);
	if (!strcmp (command[0], "cd") || !strcmp (command[0], "exit") || !strcmp (command[0], "fg") || !strcmp (command[0], "bg"))
		exit (0);
	if (!strcmp (command[0], "jobs"))
	{
		showJobs (jobs, njobs, 1);
		exit (0);
	}
	if (!strcmp (command[0], "pwd"))
	{
		char *s = getcwd (NULL, 0);
		if (s == NULL)
		{
			perror ("xish");
			exit (1);
		}
		puts (s);
		free (s);
		exit (0);
	}

	command[nparams] = NULL;
	execvp (command[0], command);
	perror (command[0]);
	exit (1);
}

/* Executes internal command */
int internalCommand (char **command, int nparams, job_t **jobs, int *njobs)
{
	if (!strcmp (command[0], "exit"))
		exit (0);
	else if (!strcmp (command[0], "cd"))
	{
		if (nparams == 1)
		{
			if (chdir (getenv ("HOME")))
				perror ("xish");
		}
		else
			if (chdir (command[1]))
				perror ("xish");
	}
	else if (!strcmp (command[0], "jobs"))
		checkJobs (jobs, njobs, 1);
	else if (!strcmp (command[0], "fg"))
	{
		int n;
		if (nparams == 1)
			n = *njobs - 1;
		else
			n = atoi (command[1]) - 1;
		if (n < 0 || n >= *njobs)
		{
			puts ("xish: no such job");
			return 0;
		}
		if (waitProcessGroup ((*jobs)[n].pgid, NULL))
			(*jobs)[n].status = ST_JUSTSTP;
		else
			deleteJob (jobs, njobs, n);
	}
	else if (!strcmp (command[0], "bg"))
	{
		int n;
		if (nparams == 1)
		{
			n = *njobs;
			while (--n >= 0 && (*jobs)[n].status != ST_STOPPED);
		}
		else
			n = atoi (command[1]) - 1;
		if (n < 0 || n >= *njobs)
		{
			puts ("No such job!");
			return 0;
		}
		kill (-(*jobs)[n].pgid, SIGCONT);
		printf ("[%d] %s\n", n + 1, (*jobs)[n].job);
	}

	return 0;
}

/* Organizes i/o redirection. Returns pid of last process in the pipeline, responsible for <, >, >> and | */
pid_t doCommands (char **params, int nparams, job_t *jobs, int njobs)
{
	pid_t pid;
	int begin = 0, divider = -1, bracketcnt = 0, j, cnt, pipes[2][2]={{0}}, fd;
	char **command;

	while (begin < nparams)
	{
		cnt = 0;
		while (++divider < nparams)
		{
			checkBracket (params[divider], &bracketcnt);
			if (bracketcnt == 0 && !strcmp (params[divider], "|"))
				break;
		}

		if (begin > 0)
		{
			if (pipes[0][0])
				close (pipes[0][0]);
			close (pipes[1][1]);
			memcpy (pipes[0], pipes[1], sizeof (pipes[1]));
		}
		if (divider < nparams)
			pipe (pipes[1]);
		if ((command = calloc (divider - begin + 1, sizeof (char *))) == NULL)
			return 0;

		if ((pid = fork ()) < 0)
			fatalError ();
		if (!pid)
		{
			if (begin > 0)
			{
				dup2 (pipes[0][0], 0);
				close (pipes[0][0]);
			}
			if (divider < nparams)
			{
				dup2 (pipes[1][1], 1);
				close (pipes[1][0]);
				close (pipes[1][1]);
			}

			for (j = begin; j < divider; ++j)		/* Cut this, brackets check neccesary */
				if (!strcmp (params[j], "<"))
				{
					fd = open (params[j + 1], O_RDONLY);
					dup2 (fd, 0);
					close (fd);
					j++;
				}
				else if (!strcmp (params[j], ">"))
				{
					fd = open (params[j + 1], O_WRONLY | O_CREAT | O_TRUNC, 0666);
					dup2 (fd, 1);
					close (fd);
					j++;
				}
				else if (!strcmp (params[j], ">>"))
				{
					fd = open (params[j + 1], O_WRONLY | O_CREAT | O_APPEND, 0666);
					dup2 (fd, 1);
					close (fd);
					j++;
				}
				else
				{
					command[cnt++] = calloc (strlen (params[j]) + 1, sizeof (char));
					strcpy (command[cnt - 1], params[j]);
				}

			command[cnt] = NULL;
			executeCommand (command, cnt, jobs, njobs);
		}

		clearParams (&command, cnt);
		begin = ++divider;
	}

	if (pipes[0][0])
		close (pipes[0][0]);
	return pid;
}

/* Executes one job in its own process group, MUST be run in fork, responsible for && and || */
void controlJob (char **command, int nparams, job_t *jobs, int njobs)
{
	int begin = 0, divider = -1, bracketcnt = 0, status = 0, res = 0;
	pid_t pid;
	setpgid (0, 0);

	signal (SIGINT,  SIG_DFL);
	signal (SIGTTOU, SIG_DFL);
	signal (SIGTSTP, SIG_DFL);

	while (begin < nparams)
	{
		while (++divider < nparams)
		{
			checkBracket (command[divider], &bracketcnt);
			if (bracketcnt == 0 && (!strcmp (command[divider], "&&") || !strcmp (command[divider], "||")))
				break;
		}
		if (begin > 0 && ((command[begin - 1][0] == '&' && res) || (command[begin - 1][0] == '|' && !res)))
		{
			begin = ++divider;
			continue;
		}

		pid = doCommands (command + begin, divider - begin, jobs, njobs);
		waitpid (pid, &status, 0);
		res = WEXITSTATUS (status);

		begin = ++divider;
	}

	exit (res);
}

/* Launches background and foreground jobs, responsible for ; and & */
int launchJobs (char **params, int nparams, job_t **jobs, int *njobs)
{
	int begin = 0, divider = -1, bracketcnt = 0, isforeground, exitstatus;
	pid_t pid;

	signal (SIGINT, SIG_IGN);
	signal (SIGTTOU, SIG_IGN);
	signal (SIGTSTP, SIG_IGN);

	while (begin < nparams)		/* Single & and ; must be caught before this! */
	{
		while (++divider < nparams)
		{
			checkBracket (params[divider], &bracketcnt);
			if (bracketcnt == 0 && (!strcmp (params[divider], "&") || !strcmp (params[divider], ";")))
				break;
		}
		isforeground = divider == nparams || params[divider][0] == ';';

		if (isforeground && isInternal (params + begin, divider - begin))  /* Needs to be changed maybe? */
		{
			internalCommand (params + begin, divider - begin, jobs, njobs);
			begin = ++divider;
			continue;
		}

		if ((pid = fork ()) < 0)
			fatalError ();
		else if (!pid)
			controlJob (params + begin, divider - begin, *jobs, *njobs);
		setpgid (pid, pid);

		if (isforeground)
		{
			if (waitProcessGroup (pid, &exitstatus))
				addJob (jobs, njobs, params + begin, divider - begin, pid, ST_JUSTSTP);
		}
		else
		{
			addJob (jobs, njobs, params + begin, divider - begin, pid, ST_RUNNING);
			exitstatus = 0;
		}

		begin = ++divider;
	}

	return exitstatus;
}

/* Sets some environmental variables for later use */
int setEnvVars ()
{
	char buf[PATH_MAX];
	int len;

	if ((len = readlink ("/proc/self/exe", buf, PATH_MAX - 1)) == -1)
		return 1;
	buf[len] = '\0';
	if (setenv ("SHELL", buf, 1) == -1)
		return 1;

	sprintf (buf, "%d", geteuid ());
	if (setenv ("EUID", buf, 1) == -1)
		return 1;

	if (getlogin_r (buf, PATH_MAX) || setenv ("USER", buf, 1) == -1)
		return 1;

	return 0;
}

/* Initialize xish */
int doInit ()
{
	if (setEnvVars ())
	{
		perror ("xish");
		return 1;
	}

	return 0;
}

/* Show input prompt */
void showPrompt ()
{
	char hostname[HOST_NAME_MAX], *cwd = getcwd (NULL, 0);
	if (gethostname (hostname, HOST_NAME_MAX) || cwd == NULL)
		printf (DFL_PROMPT);
	printf ("%s@%s %s $ ", getlogin (), hostname, cwd);
	free (cwd);
}

/* Just a main... */
int main ()
{
	int nparams, njobs = 0;
	char **params = NULL;
	job_t *jobs = NULL;
	result_t ret;

	signal (SIGINT, SIG_IGN);
	signal (SIGTTOU, SIG_IGN);
	signal (SIGTSTP, SIG_IGN);

	if (doInit ())
		return 1;
	showPrompt ();

	while ((ret = readCommand (&params, &nparams)) != RET_EOF)
	{
		if (ret == RET_ERR)
			puts ("xish: wrong command format");
		else
		{
			placeEnv (params, nparams);
			launchJobs (params, nparams, &jobs, &njobs);
		}

		clearParams (&params, nparams);
		checkJobs (&jobs, &njobs, 0);
		showPrompt ();
	}

	puts ('\n');
	clearParams (&params, nparams);
	clearJobs (&jobs, njobs);

	return 0;
}