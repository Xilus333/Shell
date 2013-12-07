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
#define CONT_PROMPT "> "

int issubshell = 0;

/* Todo:
 * -Everything halts after CTR-C
 * -Rework error handling
 */

typedef enum { RET_OK, RET_EOF, RET_MEMORYERR } result_t;

typedef enum { ST_NONE, ST_RUNNING, ST_DONE, ST_STOPPED, ST_JUSTSTP } status_t;

typedef enum { WT_WORD = 0, WT_LBRACKET, WT_RBRACKET, WT_FILERD, WT_FILEWRTRUNC, WT_FILEWRAPPEND, WT_BACKGROUND,
               WT_AND, WT_OR, WT_SEMICOLON, WT_PIPE } word_t;
#define IS_FILEOP(a) ((a) == WT_FILEWRAPPEND || (a) == WT_FILEWRTRUNC || (a) == WT_FILERD)

typedef struct
{
	char *job;
	int pgid;
	status_t status;
} job_t;

typedef struct
{
	char *word;
	word_t type;
} param_t;

/* Terminates program, for use in subshells */
void fatal_error()
{
	perror("xish");
	exit(-1);
}

/* A little functions to use with return */
int nonfatal_error(char *message)
{
	if (message == NULL)
		perror ("xish");
	else
		fprintf (stderr, message);
	return -1;
}

/* Reallocates str if it has reached maximum capacity */
int checkStringLen (char **pstr, int len)
{
	char *ptr;
	if (len % STR_SIZE > 0)
		return 0;
	if (len == 0)
		*pstr = NULL;
	if ((ptr = realloc (*pstr, (len + STR_SIZE) * sizeof (char))) == NULL)
		return nonfatal_error(NULL);
	*pstr = ptr;
	return 0;
}


/* Adds null terminator to a string, reallocates it if neccessary */
int endString (char **str, int len)
{
	char *ptr;
	if (len == 0)
		*str = NULL;
	if (len % STR_SIZE == 0)
	{
		if ((ptr = realloc (*str, (len + 1) * sizeof (char))) == NULL)
			return nonfatal_error(NULL);
		*str = ptr;
	}
	(*str)[len] = '\0';
	return 0;
}

/* Adds ch to a string */
int addChar (char **str, int *len, char ch)
{
	if (checkStringLen (str, *len) == -1)
		return -1;
	(*str)[(*len)++] = ch;
	return 0;
}

/* Realocates params if it has reached maximum capacity */
int checkParamCnt (param_t **params, int nparams)
{
	param_t *ptr;
	if (nparams % PARAM_COUNT > 0)
		return 0;
	if ((ptr = realloc (*params, (nparams + PARAM_COUNT) * sizeof (param_t))) == NULL)
		return nonfatal_error(NULL);
	*params = ptr;
	return 0;
}

/* Adds one parameter to params */
int addParam (param_t **params, int *nparams, word_t ptype)
{
	if (*nparams == 0 ? checkParamCnt (params, 0) : checkParamCnt (params, *nparams + 1))
		return -1;
	(*params)[(*nparams)++].type = ptype;
	return 0;
}

/* Frees params array */
void clearParams (param_t **params, int nparams)
{
	int i;
	if (params == NULL)
		return;
	for (i = 0; i < nparams; ++i)
		free ((*params)[i].word);
	free (*params);
	*params = NULL;
}

/* Prints params array. For testing purposes only */
void printParams (param_t *params, int nparams)
{
	int i;
	printf ("%d params: ", nparams);
	for (i = 0; i < nparams; i++)
		if (params[i].type == WT_WORD)
			printf ("%s ", params[i].word);
		else
			printf ("%d ", params[i].type);
	putchar ('\n');
}

/* Adds new entry to jobs array, initializes the structure with given data */
int addJob (job_t **jobs, int *njobs, param_t *command, int nparams, int pgid, status_t status)
{
	int len = nparams + 1, i;
	job_t *ptr;

	if ((ptr = realloc (*jobs, (*njobs + 1) * sizeof (job_t))) == NULL)
		return nonfatal_error(NULL);

	*jobs = ptr;
	ptr = *jobs + *njobs;
	ptr->pgid = pgid;
	ptr->status = status;
	++(*njobs);

	for (i = 0; i < nparams; ++i)
		if (command[i].type == WT_WORD)
			len += strlen (command[i].word);
		else
			len += 2;
	if ((ptr->job = calloc (len + 1, sizeof (char))) == NULL)
		return nonfatal_error(NULL);

	len = 0;
	for (i = 0; i < nparams; ++i)
	{
		strcpy (ptr->job + len, command[i].word);
		len = strlen (ptr->job);
		ptr->job[len] = ' ';
		ptr->job[++len] = '\0';
	}

	if (status == ST_RUNNING)
		printf ("[%d] %d\n", *njobs, ptr->pgid);

	return 0;
}

/* Deletes done entry form jobs array */
void deleteJob (job_t **jobs, int *njobs, int n)
{
	int i;
	job_t *ptr;

	if ((*jobs)[n].job != NULL)
		free ((*jobs)[n].job);
	(*jobs)[n].job = NULL;
	(*jobs)[n].status = ST_NONE;
	if (n == *njobs - 1)
	{
		i = n;
		while (((*jobs)[i].status == ST_NONE) && --i >= 0);
		if ((ptr = realloc (*jobs, (i + 1) * sizeof (job_t))) == NULL && i + 1 > 0)
			return;
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
void checkJobs (job_t **jobs, int *njobs)
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
			if (WIFSTOPPED (status))
				job->status = ST_JUSTSTP;
			else if (WIFCONTINUED (status))
				job->status = ST_RUNNING;
		if (pid == -1)
			job->status = ST_DONE;
	}
}

void deleteDoneJobs (job_t **jobs, int *njobs)
{
	int i;
	for (i = 0; i < *njobs; ++i)
		if ((*jobs)[i].status == ST_DONE)
			deleteJob (jobs, njobs, i);
}

/* Return special sequence meaning */
word_t charType (char ch, word_t prev)
{
	if (ch == '>' && prev == WT_FILEWRTRUNC) return WT_FILEWRAPPEND;
	if (ch == '|' && prev == WT_PIPE)		 return WT_OR;
	if (ch == '&' && prev == WT_BACKGROUND)	 return WT_AND;
	if (prev != 0) return WT_WORD;
	if (ch == '>') return WT_FILEWRTRUNC;
	if (ch == '<') return WT_FILERD;
	if (ch == '&') return WT_BACKGROUND;
	if (ch == '|') return WT_PIPE;
	if (ch == '(') return WT_LBRACKET;
	if (ch == ')') return WT_RBRACKET;
	if (ch == ';') return WT_SEMICOLON;
	return WT_WORD;
}

void flush_stdin()
{
	char ch;
	while ((ch = getchar()) != '\n' && ch != EOF);
}

#define MEMORYOP(a) if ((a) == -1) { flush_stdin(); return RET_MEMORYERR; }

/* Reads the infinite string and parses it into substrings array. Return statuses:
 * RET_OK  - command is correct
 * RET_EOF - EOF found
 * RET_MEMORYERR - memory allocation error
 */
result_t readCommand (param_t **params, int *nparams)
{
	enum { IN_WORD, IN_BETWEEN, IN_ESCAPE, IN_QUOTES, IN_SPECIAL } state = IN_BETWEEN, previous;
	int ch, len = 0, type, bracketcnt = 0;

	*nparams = 0;

	while (1)
	{
		ch = getchar ();
		if (ch == EOF)
			return RET_EOF;

		switch (state)
		{
			case IN_SPECIAL:
				if ((type = charType (ch, (*params)[*nparams - 1].type)) != WT_WORD)
				{
					state = IN_BETWEEN;
					(*params)[*nparams - 1].type = type;
					MEMORYOP(addChar (&(*params)[*nparams - 1].word, &len, ch));
					MEMORYOP(endString (&(*params)[*nparams - 1].word, 2));
					break;
				}

			case IN_BETWEEN:
				if (ch == '(')
					++bracketcnt;
				else if (ch == ')')
					--bracketcnt;
				if (ch == '\n' && bracketcnt > 0)
					printf(CONT_PROMPT);
				else if (ch == '\n')
					return RET_OK;
				else if (ch == '\\')
				{
					previous = IN_WORD;
					state = IN_ESCAPE;
					MEMORYOP(addParam (params, nparams, WT_WORD));
					len = 0;
				}
				else if (ch == '#')
				{
					flush_stdin();
					return RET_OK;
				}
				else if (ch == '"')
				{
					state = IN_QUOTES;
					MEMORYOP(addParam (params, nparams, WT_WORD));
					len = 0;
				}
				else if (isspace (ch))
					;
				else if ((type = charType (ch, 0)) > 0)
				{
					state = IN_SPECIAL;
					MEMORYOP(addParam (params, nparams, type));
					len = 0;
					MEMORYOP(addChar (&(*params)[*nparams - 1].word, &len, ch));
					MEMORYOP(endString (&(*params)[*nparams - 1].word, 1));
				}
				else
				{
					state = IN_WORD;
					MEMORYOP(addParam (params, nparams, WT_WORD));
					len = 0;
					MEMORYOP(addChar (&(*params)[*nparams - 1].word, &len, ch));
				}
				break;

			case IN_WORD:
				if (ch == '(')
					++bracketcnt;
				else if (ch == ')')
					--bracketcnt;
				if (ch == '\n' && bracketcnt  > 0)
					printf (CONT_PROMPT);
				else if (ch == '\n')
				{
					MEMORYOP(endString (&(*params)[*nparams - 1].word, len));
					return RET_OK;
				}
				else if (ch == '\\')
				{
					previous = IN_WORD;
					state = IN_ESCAPE;
				}
				else if (ch == '#')
				{
					flush_stdin();
					MEMORYOP(endString (&(*params)[*nparams - 1].word, len));
					return RET_OK;
				}
				else if (ch == '"')
				{
					state = IN_QUOTES;
					MEMORYOP(endString (&(*params)[*nparams - 1].word, len));
					MEMORYOP(addParam (params, nparams, WT_WORD));
					len = 0;
				}
				else if (isspace (ch))
				{
					state = IN_BETWEEN;
					MEMORYOP(endString (&(*params)[*nparams - 1].word, len));
				}
				else if ((type = charType (ch, 0)) > 0)
				{
					state = IN_SPECIAL;
					MEMORYOP(endString (&(*params)[*nparams - 1].word, len));
					MEMORYOP(addParam (params, nparams, type))
					len = 0;
					MEMORYOP(addChar (&(*params)[*nparams - 1].word, &len, ch));
					MEMORYOP(endString (&(*params)[*nparams - 1].word, 1));
				}
				else
					MEMORYOP(addChar (&(*params)[*nparams - 1].word, &len, ch));
				break;

			case IN_ESCAPE:
				state = previous;
				if (ch == '\n')
					printf(CONT_PROMPT);
				else
					MEMORYOP(addChar (&(*params)[*nparams - 1].word, &len, ch));
				break;

			case IN_QUOTES:
				if (ch == '\n')
					printf (CONT_PROMPT);
				else if (ch == '\\')
				{
					previous = IN_QUOTES;
					state = IN_ESCAPE;
				}
				else if (ch == '"')
				{
					state = IN_BETWEEN;
					MEMORYOP(endString (&(*params)[*nparams - 1].word, len));
				}
				else
					MEMORYOP(addChar (&(*params)[*nparams - 1].word, &len, ch));
				break;
		}
	}
}

/* Replaces environmental variables with their value */
int placeEnv (param_t *params, int nparams)				/* Remake this maybe? */
{
	int i;

	for (i = 0; i < nparams; ++i)
	{
		int len, posp = 0, poss = 0, ch;
		char *s;

		if (params[i].type != WT_WORD || (len = strlen (params[i].word)) < 2 || !strchr (params[i].word, '$'))
			continue;

		if ((s = calloc (STR_SIZE, sizeof (char))) == NULL)
			return nonfatal_error(NULL);
		while (posp < len)
		{
			char *env;
			int start;

			while (posp < len && params[i].word[posp] != '$')
			{
				if (checkStringLen (&s, poss) == -1)
					return -1;
				s[poss++] = params[i].word[posp++];
			}
			if (posp == len)
				break;
			start = posp + 1;

			while (++posp < len && isalpha ((int)params[i].word[posp]));

			ch = params[i].word[posp];
			params[i].word[posp] = '\0';
			if ((env = getenv (params[i].word + start)) != NULL)
			{
				if ((poss + strlen (env) + 1) / STR_SIZE > poss / STR_SIZE)
				{
					char *ptr;
					if ((ptr = realloc (s, ((poss + strlen (env) + 1) / STR_SIZE + 1) * STR_SIZE * sizeof (char))) == NULL)
					{
						perror("xish");
						free(s);
						return -1;
					}
					s = ptr;
				}
				strcpy (s + poss, env);
				poss += strlen (env);
			}
			params[i].word[posp] = ch;
		}

		endString (&s, poss);
		free (params[i].word);
		params[i].word = s;
	}
	return 0;
}

/* Checks if command is internal that must be executed in the main process */
int isInternal (param_t *params, int nparams)
{
	int i;

	if (params[0].type != WT_WORD || (strcmp (params[0].word, "cd") && strcmp (params[0].word, "exit")
	    && strcmp (params[0].word, "jobs") && strcmp (params[0].word, "fg") && strcmp (params[0].word, "bg")))
		return 0;
	for (i = 1; i < nparams; ++i)
		if (params[i].type == WT_PIPE)
			return 0;

	return 1;
}

/* Shifts process group of pid process to foreground and waits for all the processes
   to finish. Returns 1 if the process was stopped and 0 if it has termeniated */
int waitProcessGroup (pid_t lastpid, int pgid, int *status)
{
	int st, pidstatus = 0, tracemode = 0;
	pid_t pid;
	if (!issubshell)
		tracemode = WUNTRACED;

	if (!issubshell)
		tcsetpgrp (STDIN_FILENO, pgid);
	kill (-pgid, SIGCONT);
	while ((pid = waitpid (-pgid, &st, tracemode)) != (pid_t)-1 && !WIFSTOPPED (st))
		if (pid == lastpid)
			pidstatus = st;
	if (!issubshell)
		tcsetpgrp (STDIN_FILENO, getpid ());

	if (WIFSTOPPED (st))
		putchar ('\n');
	if (status != NULL && !WIFSTOPPED(st))
		*status = WEXITSTATUS (pidstatus);

	return WIFSTOPPED (st);
}

/* Finds two given diveders in range from begin to array end */
int findDivider (param_t *params, int nparams, int begin, word_t div1, word_t div2)
{
	int divider = begin - 1, bracketcnt = 0;

	while (++divider < nparams)
	{
		if (params[divider].type == WT_LBRACKET)
			++bracketcnt;
		else if (params[divider].type == WT_RBRACKET)
			--bracketcnt;
		else if (bracketcnt == 0 && (params[divider].type == div1 || params[divider].type == div2))
			break;
	}

	return divider;
}

/* For recursion */
int launchJobs (param_t *, int, job_t **, int *);

/* Executes a straightforward params (no pipes, no dividers - just params with parameters) */
void executeCommand (param_t *params, int nparams, job_t *jobs, int njobs)
{
	char **command;
	int i;

	signal (SIGINT,  SIG_DFL);
	signal (SIGTSTP, SIG_DFL);
	signal (SIGTTOU, SIG_DFL);

	if (params[0].type == WT_LBRACKET) /* Launching subshell */
	{
		issubshell = 1;
		exit (launchJobs (params + 1, nparams - 2, &jobs, &njobs));
	}

	if (!nparams || params[0].word[0] == '\0')
		exit (0);
	if (!strcmp (params[0].word, "cd") || !strcmp (params[0].word, "exit")
	    || !strcmp (params[0].word, "fg") || !strcmp (params[0].word, "bg"))
		exit (0);
	if (!strcmp (params[0].word, "jobs"))
	{
		showJobs (jobs, njobs, 1);
		exit (0);
	}
	if (!strcmp (params[0].word, "battlefield"))
	{
		puts ("Hi guys, TheWorldsEnd here!");
		exit (0);
	}
	if (!strcmp (params[0].word, "pwd"))
	{
		char *s = getcwd (NULL, 0);
		if (s == NULL)
			fatal_error();
		puts (s);
		free (s);
		exit (0);
	}

	if ((command = malloc ((nparams + 1) * sizeof (char *))) == NULL)
		fatal_error();
	for (i = 0; i < nparams; ++i)
		command[i] = params[i].word;
	free(params);
	command[nparams] = NULL;
	execvp (command[0], command);
	fprintf(stderr, "xish: ");
	perror (command[0]);
	exit (-1);
}

/* Return new command array without file redirectors */
param_t *removeRedirectors (param_t *params, int nparams, int *count)
{
	int i, bracketcnt = 0, cnt = 0;
	param_t *command;

	if ((command = calloc (nparams, sizeof (param_t))) == NULL)
	{
		perror("xish");
		return 0;
	}

	for (i = 0; i < nparams; ++i)
		if (bracketcnt == 0 && IS_FILEOP (params[i].type))
			++i;
		else
		{
			if (params[i].type == WT_LBRACKET)
					--bracketcnt;
			else if (params[i].type == WT_RBRACKET)
					++bracketcnt;
			command[cnt++] = params[i];
		}

	*count = cnt;
	return command;
}

/* Dups files from params to stdin and stdout */
int dupFiles (param_t *params, int nparams)
{
	int i, bracketcnt = 0, in = STDIN_FILENO, out = STDOUT_FILENO;

	for (i = nparams - 2; i > 0; --i)
		if (bracketcnt == 0 && IS_FILEOP (params[i].type))
		{
			if (out == 1 && params[i].type == WT_FILEWRTRUNC)
			{
				if ((out = open (params[i + 1].word, O_WRONLY | O_CREAT | O_TRUNC, 0644)) == -1)
					return nonfatal_error(NULL);
			}
			else if (out == 1 && params[i].type == WT_FILEWRAPPEND)
			{
				if ((out = open (params[i + 1].word, O_WRONLY | O_CREAT | O_APPEND, 0644)) == -1)
					return nonfatal_error(NULL);
			}
			else if (in == 0 && params[i].type == WT_FILERD)
			{
				if ((in = open (params[i + 1].word, O_RDONLY)) == -1)
					return nonfatal_error(NULL);
			}
			--i;
		}
		else if (params[i].type == WT_LBRACKET)
				--bracketcnt;
		else if (params[i].type == WT_RBRACKET)
				++bracketcnt;

	if (in  != STDIN_FILENO)  { dup2 (in, 0);  close (in);  }
	if (out != STDOUT_FILENO) { dup2 (out, 1); close (out); }

	return 0;
}

/* cd internal commmand */
int internalChangeDir (param_t *params, int nparams)
{
	char *s;
	if (nparams > 1)
		s = params[1].word;
	else
		s = getenv ("HOME");
	if (chdir (s))
		return nonfatal_error(NULL);
	return 0;
}

/* jobs internal command */
int internalJobs (param_t *params, int nparams, job_t **jobs, int *njobs)
{
	if (issubshell)
		return nonfatal_error("xish: jobs: no job control\n");
	checkJobs (jobs, njobs);
	showJobs (*jobs, *njobs, 1);
	deleteDoneJobs (jobs, njobs);
	return 0;
}

/* fg internal command */
int internalForeground (param_t *params, int nparams, job_t **jobs, int *njobs)
{
	int n;
	if (issubshell)
		return nonfatal_error("xish: fg: no job control\n");

	if (nparams == 1)
		n = *njobs - 1;
	else
		n = atoi (params[1].word) - 1;
	if (n < 0 || n >= *njobs)
		return nonfatal_error("xish: no such job\n");

	if (waitProcessGroup (0, (*jobs)[n].pgid, NULL))
		(*jobs)[n].status = ST_JUSTSTP;
	else
		deleteJob (jobs, njobs, n);

	return 0;
}

/* bg internal command */
int internalBackground (param_t *params, int nparams, job_t **jobs, int *njobs)
{
	int n;
	if (issubshell)
		return nonfatal_error("xish: bg: no job control\n");

	if (nparams == 1)
	{
		n = *njobs;
		while (--n >= 0 && (*jobs)[n].status != ST_STOPPED);
	}
	else
		n = atoi (params[1].word) - 1;

	if (n < 0 || n >= *njobs)
		return nonfatal_error("xish: no such job\n");
	kill (-(*jobs)[n].pgid, SIGCONT);
	printf ("[%d] %s\n", n + 1, (*jobs)[n].job);

	return 0;
}

/* Executes internal command */
int internalCommand (param_t *params, int nparams, job_t **jobs, int *njobs)
{
	int savestdin, savestdout, count, result;
	param_t *command;
	savestdin =  dup (STDIN_FILENO);
	savestdout = dup (STDOUT_FILENO);

	if (dupFiles (params, nparams) == -1 || (command = removeRedirectors (params, nparams, &count)) == NULL)
	{
		dup2 (savestdin,  STDIN_FILENO);
		dup2 (savestdout, STDOUT_FILENO);
		return -1;
	}

	if (!strcmp (command[0].word, "exit"))
		exit (0);
	else if (!strcmp (command[0].word, "cd"))
		result = internalChangeDir (command, count);
	else if (!strcmp (command[0].word, "jobs"))
		result = internalJobs (command, count, jobs, njobs);
	else if (!strcmp (command[0].word, "fg"))
		result = internalForeground (command, count, jobs, njobs);
	else if (!strcmp (command[0].word, "bg"))
		result = internalBackground (command, count, jobs, njobs);

	dup2 (savestdin,  STDIN_FILENO);
	dup2 (savestdout, STDOUT_FILENO);
	return result;
}

/* Organizes i/o redirection. Returns pid of last process in the pipeline, responsible for <, >, >> and | */
pid_t doCommands (param_t *params, int nparams, job_t *jobs, int njobs)
{
	pid_t pgid = getpgid (0), pid;
	int begin = 0, divider, count, pipes[2][2]={{0}};
	param_t *command;

	while (begin < nparams)
	{
		divider = findDivider (params, nparams, begin, WT_PIPE, WT_PIPE);

		if (begin > 0)
		{
			if (pipes[0][0])
				close (pipes[0][0]);
			close (pipes[1][1]);
			pipes[0][0] = pipes[1][0];
		}
		if (divider < nparams)
			pipe (pipes[1]);

		if ((pid = fork ()) < 0)
			return (pid_t)nonfatal_error(NULL);
		if (begin == 0 && !issubshell)
			pgid = pid;
		if (!pid)
		{
			setpgid (0, pgid);

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

			if (dupFiles (params + begin, divider - begin) == -1)
				exit (-1);
			if ((command = removeRedirectors (params + begin, divider - begin, &count)) == NULL)
				exit (-1);
			executeCommand (command, count, jobs, njobs);
		}
		setpgid (pid, pgid);
		begin = divider + 1;
	}

	if (pipes[0][0])
		close (pipes[0][0]);
	return pid;
}

/* Executes one job in its own process group, responsible for && and || */
int controlJob (param_t *params, int nparams, int isforeground, job_t **jobs, int *njobs)
{
	int begin = 0, divider, exitstatus = 0;
	pid_t pid, pgid;

	while (begin < nparams)
	{
		divider = findDivider (params, nparams, begin, WT_AND, WT_OR);
		if (begin > 0 && ((params[begin - 1].type == WT_AND && exitstatus) || (params[begin - 1].type == WT_OR && !exitstatus)))
		{
			begin = divider + 1;
			continue;
		}

		if (isforeground && isInternal (params + begin, divider - begin))
		{
			exitstatus = internalCommand (params + begin, divider - begin, jobs, njobs);
			begin = divider + 1;
			continue;
		}

		if ((pid = doCommands (params + begin, divider - begin, *jobs, *njobs)) == (pid_t)-1)
		{
			exitstatus = -1;
			begin = divider + 1;
			continue;
		}
		pgid = getpgid (pid);
		exitstatus = -1;

		if (isforeground)
		{
			if (waitProcessGroup (pid, pgid, &exitstatus))
				addJob (jobs, njobs, params + begin, divider - begin, pgid, ST_JUSTSTP);
		}
		else if (!issubshell)
			addJob (jobs, njobs, params + begin, divider - begin, pgid, ST_RUNNING);

		begin = divider + 1;
	}

	return exitstatus;
}

/* Launches background and foreground jobs, responsible for ; and & */
int launchJobs (param_t *params, int nparams, job_t **jobs, int *njobs)
{
	int begin = 0, divider, isforeground, needcontrol, exitstatus;
	pid_t pid;

	if (issubshell)
		signal (SIGTTOU, SIG_IGN);

	while (begin < nparams)
	{
		divider = findDivider (params, nparams, begin, WT_BACKGROUND, WT_SEMICOLON);
		isforeground = divider == nparams || params[divider].type == WT_SEMICOLON;
		needcontrol = !isforeground && findDivider (params + begin, divider - begin, 0, WT_AND, WT_OR) < divider - begin;

		if (needcontrol)
		{
			if ((pid = fork ()) < 0)
				return nonfatal_error(NULL);
			if (!pid)
			{
				setpgid (0, 0);
				issubshell = 1;
				exit (launchJobs (params, nparams - 1, jobs, njobs));
			}

			setpgid (pid, pid);
			addJob (jobs, njobs, params + begin, divider - begin, pid, ST_RUNNING);
		}
		else
			exitstatus = controlJob (params + begin, divider - begin, isforeground, jobs, njobs);

		begin = divider + 1;
	}

	return exitstatus;
}

/* Checks if syntax is correct. Returns 1 on correct syntax */
int checkSyntax (param_t *params, int nparams)
{
	int i, bracketcnt = 0, nospecial = 1, noend = 1;

	if (nparams == 0)
		return 1;

	for (i = 0; i < nparams; ++i)
	{
		if (params[i].type == WT_LBRACKET)
		{
			++bracketcnt;
			nospecial = noend = 1;
		}
		else if (params[i].type == WT_RBRACKET)
		{
			if (noend || --bracketcnt < 0)
				break;
			nospecial = noend = 0;
		}
		else if (nospecial && params[i].type != WT_WORD)
			break;
		else if (params[i].type == WT_BACKGROUND)
		{
			nospecial = 1;
			noend = 0;
		}
		else if (params[i].type != WT_WORD)
			nospecial = noend = 1;
		else
			nospecial = noend = 0;
	}

	if (i < nparams || bracketcnt > 0 || noend)
	{
		if (i < nparams)
			fprintf (stderr, "xish: syntax error near %s\n", params[i].word);
		else
			fprintf (stderr, "xish: unexpected end of file\n");
		return 0;
	}

	return 1;
}

/* Sets some environmental variables for later use */
int setEnvVars ()
{
	char buf[PATH_MAX];
	int len;

	if ((len = readlink ("/proc/self/exe", buf, PATH_MAX - 1)) == -1)
		return nonfatal_error(NULL);
	buf[len] = '\0';
	if (setenv ("SHELL", buf, 1) == -1)
		return nonfatal_error(NULL);
	sprintf (buf, "%d", geteuid ());
	if (setenv ("EUID", buf, 1) == -1)
		return nonfatal_error(NULL);
	if (getlogin_r (buf, PATH_MAX) || setenv ("USER", buf, 1) == -1)
		return nonfatal_error(NULL);

	return 0;
}

/* Initialize xish */
int doInit ()
{
	if (setEnvVars ())
		return -1;
	return 0;
}

/* Show input prompt */
void showPrompt ()
{
	char hostname[HOST_NAME_MAX], *cwd = getcwd (NULL, 0);
	if (gethostname (hostname, HOST_NAME_MAX) || cwd == NULL)
	{
		printf (DFL_PROMPT);
		if (cwd != NULL)
			free (cwd);
		return;
	}
	printf ("%s@%s %s $ ", getlogin (), hostname, cwd);
	free (cwd);
}

/* Just a main */
int main ()
{
	int nparams, njobs = 0;
	param_t *params = NULL;
	job_t *jobs = NULL;
	result_t result;

	signal (SIGINT,  SIG_IGN);
	signal (SIGTSTP, SIG_IGN);
	signal (SIGTTOU, SIG_IGN);

	if (doInit() == -1)
		fprintf(stderr, "xish: initialisation failed, it is not advised to continue\n");
	showPrompt ();

	while ((result = readCommand (&params, &nparams)) != RET_EOF)
	{
		if (result != RET_MEMORYERR && checkSyntax (params, nparams) && placeEnv (params, nparams) != -1)
			launchJobs (params, nparams, &jobs, &njobs);
		checkJobs (&jobs, &njobs);
		showJobs (jobs, njobs, 0);
		deleteDoneJobs (&jobs, &njobs);
		showPrompt ();
		clearParams (&params, nparams);
	}

	putchar ('\n');
	clearParams (&params, nparams);
	clearJobs (&jobs, njobs);

	return 0;
}