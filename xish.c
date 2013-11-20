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

/* Todo:
 * -New line after CTR-C?
 * -Move checkJobs, so "jobs" could be redirected?
 * -Rework internal command handling
 */

typedef enum { RET_OK, RET_ERR, RET_EOF } result_t;
typedef enum { ST_NONE, ST_RUNNING, ST_DONE, ST_STOPPED, ST_JUSTSTP } status_t;
typedef enum { WT_WORD = 0, WT_LBRACKET, WT_RBRACKET, WT_FILERD, WT_FILEWRTRUNC, WT_FILEWRAPPEND, WT_BACKGROUND,
               WT_AND, WT_OR, WT_SEMICOLON, WT_PIPE } word_t;
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

FILE *file;

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
	if (len == 0)
		*str = NULL;
	if ((ptr = realloc (*str, (len + STR_SIZE) * sizeof (char))) == NULL)
			fatalError ();
	*str = ptr;
}


/* Adds null terminator to a string, reallocates it if neccessary */
void endString (char **str, int len)
{
	char *ptr;

	if (len == 0)
		*str = NULL;
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

/* Realocates params if it has reached maximum capacity */
void checkParamCnt (param_t **params, int nparams)
{
	param_t *ptr;

	if (nparams % PARAM_COUNT > 0)
		return;
	if ((ptr = realloc (*params, (nparams + PARAM_COUNT) * sizeof (param_t))) == NULL)
		fatalError ();

	*params = ptr;
}

/* Adds one parameter to params */
void addParam (param_t **params, int *nparams, word_t ptype)
{
	if (*nparams == 0)
		checkParamCnt (params, 0);
	else
		checkParamCnt (params, *nparams + 1);
	(*params)[(*nparams)++].type = ptype;
}

/* Frees params array */
void clearParams (param_t **params, int nparams)
{
	int i;

	if (params == NULL)
		return;
	for (i = 0; i < nparams; ++i)
		if ((*params)[i].type == WT_WORD)
			free ((*params)[i].word);
	free (*params);

	*params = NULL;
}

/* Prints params array. For testing puroses only */
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

char *charDivider (word_t type, char *s)
{
	s[1] = '\0';
	switch (type)
	{
		case WT_WORD: 		  break;
		case WT_LBRACKET:	  s[0] = '('; break;
		case WT_RBRACKET:	  s[0] = ')'; break;
		case WT_FILERD:		  s[0] = '<'; break;
		case WT_FILEWRTRUNC:  s[0] = '>'; break;
		case WT_FILEWRAPPEND: s[0] = '>'; s[1] = '>'; s[2] = '\0'; break;
		case WT_BACKGROUND:	  s[0] = '&'; break;
		case WT_AND:		  s[0] = '&'; s[1] = '&'; s[2] = '\0'; break;
		case WT_OR:			  s[0] = '|'; s[1] = '|'; s[2] = '\0'; break;
		case WT_SEMICOLON:	  s[0] = ';'; break;
		case WT_PIPE:		  s[0] = '|'; break;
	}
	return s;
}

/* Adds new entry to jobs array, initializes the structure with given data */
void addJob (job_t **jobs, int *njobs, param_t *command, int nparams, int pgid, status_t status)
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
		if (command[i].type == WT_WORD)
			len += strlen (command[i].word);
		else
			len += 2;
	if ((ptr->job = calloc (len + 1, sizeof (char))) == NULL)
		fatalError ();

	len = 0;
	for (i = 0; i < nparams; ++i)
	{
		if (command[i].type == WT_WORD)
			strcpy (ptr->job + len, command[i].word);
		else
			charDivider (command[i].type, ptr->job + len);
		len = strlen (ptr->job);
		ptr->job[len] = ' ';
		ptr->job[++len] = '\0';
	}

	if (status == ST_RUNNING)
		printf ("[%d] %d\n", *njobs + 1, ptr->pgid);
	++(*njobs);
}

/* Deletes done entry form jobs array */
void deleteJob (job_t **jobs, int *njobs, int n)
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

/* Return special sequence meaning */
word_t charType (char ch, word_t prev)
{
	if (ch == '>' && prev == WT_FILEWRTRUNC) return WT_FILEWRAPPEND;
	if (ch == '|' && prev == WT_PIPE)		 return WT_OR;
	if (ch == '&' && prev == WT_BACKGROUND)	 return WT_AND;
	if (prev != 0) return 0;
	if (ch == '>') return WT_FILEWRTRUNC;
	if (ch == '<') return WT_FILERD;
	if (ch == '&') return WT_BACKGROUND;
	if (ch == '|') return WT_PIPE;
	if (ch == '(') return WT_LBRACKET;
	if (ch == ')') return WT_RBRACKET;
	if (ch == ';') return WT_SEMICOLON;
	return 0;
}

/* Reads the infinite string and parses it into substrings array. Return statuses:
 * RET_OK  - command is correct
 * RET_ERR - wrong command format
 * RET_EOF - EOF found
 * RET_MEMORYERR - memory allocation error
 */
result_t readCommand (param_t **params, int *nparams)
{
	enum { IN_INITIAL, IN_WORD, IN_BETWEEN, IN_ESCAPE, IN_QUOTES, IN_COMMENT, IN_ERROR, IN_SPECIAL } state = IN_INITIAL, previous;
	int ch, len = 0, type;

	*nparams = 0;

	while (1)
	{
		ch = getchar ();
		if (ch == EOF && state == IN_INITIAL)
			return RET_EOF;
		else if (ch == EOF)
			continue;

		switch (state)
		{
			case IN_INITIAL:
				if (ch == '\n' || ch == '#')
				{
					addParam (params, nparams, WT_WORD);
					endString (&(*params)[*nparams - 1].word, 0);
				}

			case IN_SPECIAL:
				if (state == IN_SPECIAL && (type = charType (ch, (*params)[*nparams - 1].type)))
				{
					state = IN_BETWEEN;
					(*params)[*nparams - 1].type = type;
					break;
				}

			case IN_BETWEEN:
				if (ch == '\n')
					return RET_OK;
				else if (ch == '\\')
				{
					previous = IN_WORD;
					state = IN_ESCAPE;
					addParam (params, nparams, WT_WORD);
					len = 0;
				}
				else if (ch == '#')
					state = IN_COMMENT;
				else if (ch == '"')
				{
					state = IN_QUOTES;
					addParam (params, nparams, WT_WORD);
					len = 0;
				}
				else if (isspace (ch))
					;
				else if ((type = charType (ch, 0)) > 0)
				{
					state = IN_SPECIAL;
					addParam (params, nparams, type);
				}
				else
				{
					state = IN_WORD;
					addParam (params, nparams, WT_WORD);
					len = 0;
					addChar (&(*params)[*nparams - 1].word, &len, ch);
				}
				break;

			case IN_WORD:
				if (ch == '\n')
				{
					endString (&(*params)[*nparams - 1].word, len);
					return RET_OK;
				}
				else if (ch == '\\')
				{
					previous = IN_WORD;
					state = IN_ESCAPE;
				}
				else if (ch == '#')
				{
					state = IN_COMMENT;
					endString (&(*params)[*nparams - 1].word, len);
				}
				else if (ch == '"')
				{
					state = IN_QUOTES;
					endString (&(*params)[*nparams - 1].word, len);
					addParam (params, nparams, WT_WORD);
					len = 0;
				}
				else if (isspace (ch))
				{
					state = IN_BETWEEN;
					endString (&(*params)[*nparams - 1].word, len);
				}
				else if ((type = charType (ch, 0)) > 0)
				{
					state = IN_SPECIAL;
					endString (&(*params)[*nparams - 1].word, len);
					addParam (params, nparams, type);
				}
				else
					addChar (&(*params)[*nparams - 1].word, &len, ch);
				break;

			case IN_ESCAPE:
				state = previous;
				if (ch == '\n')
					putchar ('>');
				else
					addChar (&(*params)[*nparams - 1].word, &len, ch);
				break;

			case IN_QUOTES:
				if (ch == '\n')
				{
					putchar ('>');
					break;
				}
				else if (ch == '\\')
				{
					previous = IN_QUOTES;
					state = IN_ESCAPE;
				}
				else if (ch == '"')
				{
					state = IN_BETWEEN;
					endString (&(*params)[*nparams - 1].word, len);
				}
				else
					addChar (&(*params)[*nparams - 1].word, &len, ch);
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
void placeEnv (param_t *params, int nparams)
{
	int i;

	for (i = 0; i < nparams; ++i)
	{
		int len, posp = 0, poss = 0, ch;
		char *s;

		if (params[i].type != WT_WORD || len < 2 || !strchr (params[i].word, '$'))
			continue;

		len = strlen (params[i].word);
		if ((s = calloc (STR_SIZE, sizeof (char))) == NULL)
			fatalError ();

		while (posp < len)
		{
			char *env;
			int start;

			while (posp < len && params[i].word[posp] != '$')
			{
				checkStringLen (&s, poss);
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
						fatalError ();
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
}

/* Checks if command is internal that must be executed in the main process */
int isInternal (param_t *command, int nparams)
{
	int i;

	if (command[0].type != WT_WORD)
		return 0;
	if (strcmp (command[0].word, "cd") && strcmp (command[0].word, "exit") && strcmp (command[0].word, "jobs") && strcmp (command[0].word, "fg")
	    && strcmp (command[0].word, "bg"))
		return 0;
	for (i = 1; i < nparams; ++i)
		if (command[i].word[0] == '|')
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

int launchJobs (param_t *, int, job_t **, int *);

/* Executes a straightforward command (no pipes, no dividers - just command with parameters) */
void executeCommand (param_t *command, int nparams, job_t *jobs, int njobs)
{
	char **params;
	int i;

	if (command[0].type == WT_LBRACKET) /* Launching subshell */
		exit (launchJobs (command + 1, nparams - 2, &jobs, &njobs));

	if (!nparams || command[0].word[0] == '\0')
		exit (0);
	if (!strcmp (command[0].word, "cd") || !strcmp (command[0].word, "exit") || !strcmp (command[0].word, "fg") || !strcmp (command[0].word, "bg"))
		exit (0);
	if (!strcmp (command[0].word, "jobs"))
	{
		showJobs (jobs, njobs, 1);
		exit (0);
	}
	if (!strcmp (command[0].word, "battlefield"))
	{
		puts ("Hi guys, xiluscap here! Don't press CTR-C yet!");
		exit (0);
	}
	if (!strcmp (command[0].word, "pwd"))
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

	params = malloc ((nparams + 1) * sizeof (char *));
	for (i = 0; i < nparams; ++i)
		params[i] = command[i].word;
	params[nparams] = NULL;
	execvp (params[0], params);
	perror (params[0]);
	exit (1);
}

/* Executes internal command */
int internalCommand (param_t *command, int nparams, job_t **jobs, int *njobs)
{
	if (!strcmp (command[0].word, "exit"))
		exit (0);
	else if (!strcmp (command[0].word, "cd"))
	{
		if (nparams == 1)
		{
			if (chdir (getenv ("HOME")))
				perror ("xish");
		}
		else
			if (chdir (command[1].word))
				perror ("xish");
	}
	else if (!strcmp (command[0].word, "jobs"))
		checkJobs (jobs, njobs, 1);
	else if (!strcmp (command[0].word, "fg"))
	{
		int n;
		if (nparams == 1)
			n = *njobs - 1;
		else
			n = atoi (command[1].word) - 1;
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
	else if (!strcmp (command[0].word, "bg"))
	{
		int n;
		if (nparams == 1)
		{
			n = *njobs;
			while (--n >= 0 && (*jobs)[n].status != ST_STOPPED);
		}
		else
			n = atoi (command[1].word) - 1;
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
pid_t doCommands (param_t *params, int nparams, job_t *jobs, int njobs)
{
	pid_t pid;
	int begin = 0, divider, bracketcnt = 0, j, cnt, pipes[2][2]={{0}}, fd;
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
		{
			perror ("xish");
			return -1;
		}
		if (!pid)
		{
			if ((command = calloc (divider - begin + 1, sizeof (param_t))) == NULL)
				fatalError ();

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

			bracketcnt = 0;
			cnt = 0;
			for (j = begin; j < divider; ++j)
				if (bracketcnt == 0 &&
						(params[j].type == WT_FILEWRTRUNC || params[j].type == WT_FILERD || params[j].type == WT_FILEWRAPPEND))
				{
					if (params[j].type == WT_FILEWRTRUNC)
					{
						fd = open (params[j + 1].word, O_WRONLY | O_CREAT | O_TRUNC, 0666);
						dup2 (fd, 1);
					}
					else if (params[j].type == WT_FILEWRAPPEND)
					{
						fd = open (params[j + 1].word, O_WRONLY | O_CREAT | O_APPEND, 0666);
						dup2 (fd, 1);
					}
					else
					{
						fd = open (params[j + 1].word, O_RDONLY);
						dup2 (fd, 0);
					}
					close (fd);
					++j;
				}
				else
				{
					if (params[j].type == WT_LBRACKET)
						++bracketcnt;
					else if (params[j].type == WT_RBRACKET)
						--bracketcnt;
					command[cnt++] = params[j];
				}

			executeCommand (command, cnt, jobs, njobs);
		}

		begin = divider + 1;
	}

	if (pipes[0][0])
		close (pipes[0][0]);
	return pid;
}

/* Executes one job in its own process group, MUST be run in fork, responsible for && and || */
void controlJob (param_t *command, int nparams, job_t *jobs, int njobs)
{
	int begin = 0, divider, status = 0, res = 0;
	pid_t pid;
	setpgid (0, 0);

	signal (SIGINT,  SIG_DFL);
	signal (SIGTTOU, SIG_DFL);
	signal (SIGTSTP, SIG_DFL);

	while (begin < nparams)
	{
		divider = findDivider (command, nparams, begin, WT_AND, WT_OR);
		if (begin > 0 && ((command[begin - 1].type == WT_AND && res) || (command[begin - 1].type == WT_OR && !res)))
		{
			begin = divider + 1;
			continue;
		}

		if ((pid = doCommands (command + begin, divider - begin, jobs, njobs)) < 0)
			exit (1);
		waitpid (pid, &status, 0);
		res = WEXITSTATUS (status);

		begin = divider + 1;
	}

	exit (res);
}

/* Launches background and foreground jobs, responsible for ; and & */
int launchJobs (param_t *params, int nparams, job_t **jobs, int *njobs)
{
	int begin = 0, divider, isforeground, exitstatus;
	pid_t pid;

	signal (SIGINT, SIG_IGN);
	signal (SIGTTOU, SIG_IGN);
	signal (SIGTSTP, SIG_IGN);

	while (begin < nparams)
	{
		divider = findDivider (params, nparams, begin, WT_BACKGROUND, WT_SEMICOLON);
		isforeground = divider == nparams || params[divider].type == WT_SEMICOLON;

		if (isforeground && isInternal (params + begin, divider - begin))  /* Needs to be changed maybe? */
		{
			internalCommand (params + begin, divider - begin, jobs, njobs);
			begin = divider + 1;
			continue;
		}

		if ((pid = fork ()) < 0)
		{
			perror ("xish");
			return 1;
		}
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

		begin = divider + 1;
	}

	return exitstatus;
}

/* Checks if syntax is correct. Returns 1 on correct syntax */
int checkSyntax (param_t *params, int nparams)
{
	int i, bracketcnt = 0, nospecial = 1, noend = 1;
	char divider[3];

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
			printf ("xish: syntax error near %s\n", charDivider (params[i].type, divider));
		else
			printf ("xish: unexpected end of file\n");
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
	{
		printf (DFL_PROMPT);
		if (cwd != NULL)
			free (cwd);
		return;
	}
	printf ("%s@%s %s $ ", getlogin (), hostname, cwd);
	free (cwd);
}

/* Just a main... */
int main ()
{
	int nparams, njobs = 0;
	param_t *params = NULL;
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
		else if (checkSyntax (params, nparams))
		{
			placeEnv (params, nparams);
			launchJobs (params, nparams, &jobs, &njobs);
		}

		clearParams (&params, nparams);
		checkJobs (&jobs, &njobs, 0);
		showPrompt ();
	}

	putchar ('\n');
	clearParams (&params, nparams);
	clearJobs (&jobs, njobs);

	return 0;
}