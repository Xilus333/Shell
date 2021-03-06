#include <sys/types.h>
#include <unistd.h>
#include <sys/wait.h>
#include <fcntl.h>
#include <error.h>
#include <errno.h>

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

typedef enum { RET_OK, RET_EOF, RET_MEMORYERR } result_t;

typedef enum { ST_NONE, ST_RUNNING, ST_DONE, ST_STOPPED, ST_JUSTSTP } status_t;

typedef enum { WT_WORD = 0, WT_LBRACKET, WT_RBRACKET, WT_FILERD, WT_FILEWRTRUNC, WT_FILEWRAPPEND, WT_BACKGROUND,
               WT_AND, WT_OR, WT_SEMICOLON, WT_PIPE } word_t;

/* Checks if word is '>', '<' or '>>' */
#define IS_FILEOP(a) ((a) == WT_FILEWRAPPEND || (a) == WT_FILEWRTRUNC || (a) == WT_FILERD)

/* Struct for storing job information */
typedef struct
{
	char *job;
	int pgid;
	status_t status;
} job_t;

/* Struct for storing parameters */
typedef struct
{
	char *word;
	word_t type;
} param_t;

/* Terminates program, for use in subshells */
void fatalError()
{
	error(-1, errno, NULL);
}

/* A little functions to use with return */
int nonfatalError(int errornum, char *message)
{
	error(0, errornum, message);
	return -1;
}

/* Reallocates str if it has reached maximum capacity */
int checkStringLen(char **pstr, int len)
{
	char *ptr;
	if (len % STR_SIZE > 0)
		return 0;
	if (len == 0)
		*pstr = NULL;
	if ((ptr = realloc(*pstr, (len + STR_SIZE) * sizeof(char))) == NULL)
		return nonfatalError(errno, NULL);
	*pstr = ptr;
	return 0;
}

/* Adds null terminator to a string, reallocates it if neccessary */
int endString(char **str, int len)
{
	char *ptr;
	if (len == 0)
		*str = NULL;
	if (len % STR_SIZE == 0)
	{
		if ((ptr = realloc(*str, (len + 1) * sizeof(char))) == NULL)
			return nonfatalError(errno, NULL);
		*str = ptr;
	}
	(*str)[len] = '\0';
	return 0;
}

/* Adds ch to a string */
int addChar(char **str, int *len, char ch)
{
	if (checkStringLen(str, *len) == -1)
		return -1;
	(*str)[(*len)++] = ch;
	return 0;
}

/* Realocates params if it has reached maximum capacity */
int checkParamCnt(param_t **params, int nparams)
{
	param_t *ptr;
	if (nparams % PARAM_COUNT > 0)
		return 0;
	if ((ptr = realloc(*params, (nparams + PARAM_COUNT) * sizeof(param_t))) == NULL)
		return nonfatalError(errno, NULL);
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
void clearParams(param_t **params, int nparams)
{
	int i;
	if (params == NULL)
		return;
	for (i = 0; i < nparams; ++i)
		free((*params)[i].word);
	free(*params);
	*params = NULL;
}

/* Prints params array. For testing purposes only */
void printParams(param_t *params, int nparams)
{
	int i;
	fprintf(stderr, "%d params: ", nparams);
	for (i = 0; i < nparams; i++)
		if (params[i].type == WT_WORD)
			printf("%s ", params[i].word);
		else
			printf("%d ", params[i].type);
	putchar('\n');
}

/* Adds new entry to jobs array, initializes the structure with given data */
int addJob(job_t **jobs, int *njobs, param_t *command, int nparams, int pgid, status_t status)
{
	int len = nparams + 1, i;
	job_t *ptr;

	if ((ptr = realloc(*jobs, (*njobs + 1) * sizeof(job_t))) == NULL)
		return nonfatalError(errno, NULL);

	*jobs = ptr;
	ptr = *jobs + *njobs;
	ptr->pgid = pgid;
	ptr->status = status;
	++(*njobs);

	for (i = 0; i < nparams; ++i)
		len += strlen(command[i].word);
	if ((ptr->job = calloc(len + 1, sizeof(char))) == NULL)
		return nonfatalError(errno, NULL);

	len = 0;
	for (i = 0; i < nparams; ++i)
	{
		strcpy(ptr->job + len, command[i].word);
		len = strlen (ptr->job);
		ptr->job[len] = ' ';
		ptr->job[++len] = '\0';
	}

	if (status == ST_RUNNING)
		printf("[%d] %d\n", *njobs, ptr->pgid);

	return 0;
}

/* Deletes done entry form jobs array */
void deleteJob(job_t **jobs, int *njobs, int jobnum)
{
	int lastdone;
	job_t *ptr;

	if ((*jobs)[jobnum].job != NULL)
		free((*jobs)[jobnum].job);
	(*jobs)[jobnum].job = NULL;
	(*jobs)[jobnum].status = ST_NONE;
	if (jobnum == *njobs - 1)
	{
		lastdone = jobnum;
		while (((*jobs)[lastdone].status == ST_NONE) && --lastdone >= 0);
		if ((ptr = realloc(*jobs, (lastdone + 1) * sizeof(job_t))) == NULL && lastdone + 1 > 0)
			return;
		*jobs = ptr;
		*njobs = lastdone + 1;
	}
}

/* Frees jobs array */
void clearJobs(job_t **jobs, int njobs)
{
	int i;

	if (*jobs == NULL)
		return;
	for (i = 0; i < njobs; ++i)
		if ((*jobs)[i].job != NULL)
			free((*jobs)[i].job);
	free(*jobs);
	*jobs = NULL;
}

/* Show current jobs status */
void showJobs(job_t *jobs, int njobs, int fullog)
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
			case ST_DONE: 	 strcpy(status, "Done");	break;
			case ST_RUNNING: strcpy(status, "Running"); break;
			case ST_JUSTSTP: jobs[i].status = ST_STOPPED;
			case ST_STOPPED: strcpy(status, "Stopped"); break;
		}
		printf("[%d] %s\t\t%s\n", i + 1, status, jobs[i].job);
	}
}

/* Checks jobs statuses. fullog determines if all the jobs should be shown, or only the done and just stopped ones */
void checkJobs(job_t **jobs, int *njobs)
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

/* Deletes done jobs from jobs array */
void deleteDoneJobs(job_t **jobs, int *njobs)
{
	int i;
	for (i = 0; i < *njobs; ++i)
		if ((*jobs)[i].status == ST_DONE)
			deleteJob (jobs, njobs, i);
}

/* Return special sequence meaning */
word_t charType(char ch, word_t prev)
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

/* Replaces environmental variable with its value */
int placeEnv(char **param, int *len, char *environmental)
{
	char *env = getenv(environmental), *ptr;
	int envlen;

	if (env == NULL)
		return 0;
	envlen = strlen(env);

	if (addChar(param, len, '\0') == -1)
		return -1;
	*len += envlen - 1;
	if ((ptr = realloc(*param, ((*len / STR_SIZE + 1) * STR_SIZE) * sizeof(char))) == NULL)
		return nonfatalError(errno, NULL);
	*param = ptr;
	strncpy(*param + *len - envlen, env, envlen);

	return 0;
}

/* Removes everything in stdin until EOF or EOL */
void flushStdin()
{
	char ch;
	while ((ch = getchar()) != '\n' && ch != EOF);
}

/* Little macros to check for memory errors in readCommand() */
#define MEMORYOP(a) if ((a) == -1) { flushStdin(); return RET_MEMORYERR; }

/* Reads the infinite string and parses it into substrings array. Return statuses:
 * RET_OK  - command is correct
 * RET_EOF - EOF found
 * RET_MEMORYERR - memory allocation error
 */
result_t readCommand(param_t **params, int *nparams)
{
	enum { IN_WORD, IN_BETWEEN, IN_ESCAPE, IN_QUOTES, IN_SPECIAL, IN_ENV } state = IN_BETWEEN, previous;
	int ch, len, type, envlen, bracketcnt = 0;
	char *environmental = NULL;

	*nparams = 0;

	while (1)
	{
		ch = getchar();
		if (ch == EOF)
			return RET_EOF;

		switch (state)
		{
			case IN_SPECIAL:
				if ((type = charType(ch, (*params)[*nparams - 1].type)) != WT_WORD)
				{
					state = IN_BETWEEN;
					(*params)[*nparams - 1].type = type;
					MEMORYOP(addChar(&(*params)[*nparams - 1].word, &len, ch));
					MEMORYOP(endString(&(*params)[*nparams - 1].word, 2));
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
					len = 0;
					MEMORYOP(addParam(params, nparams, WT_WORD));
				}
				else if (ch == '#')
				{
					flushStdin();
					return RET_OK;
				}
				else if (ch == '"')
				{
					state = IN_QUOTES;
					len = 0;
					MEMORYOP(addParam(params, nparams, WT_WORD));
				}
				else if (isspace (ch))
					;
				else if ((type = charType(ch, 0)) > 0)
				{
					state = IN_SPECIAL;
					len = 0;
					MEMORYOP(addParam(params, nparams, type));
					MEMORYOP(addChar(&(*params)[*nparams - 1].word, &len, ch));
					MEMORYOP(endString(&(*params)[*nparams - 1].word, 1));
				}
				else if (ch == '$')
				{
					state = IN_ENV;
					len = 0;
					envlen = 0;
					MEMORYOP(addParam(params, nparams, WT_WORD));
				}
				else
				{
					state = IN_WORD;
					len = 0;
					MEMORYOP(addParam(params, nparams, WT_WORD));
					MEMORYOP(addChar(&(*params)[*nparams - 1].word, &len, ch));
				}
				break;

			case IN_ENV:
				if (isalnum(ch))
				{
					MEMORYOP(addChar(&environmental, &envlen, ch));
					break;
				}
				state = IN_WORD;
				MEMORYOP(endString(&environmental, envlen));
				MEMORYOP(placeEnv(&(*params)[*nparams - 1].word, &len, environmental));
				free(environmental);

			case IN_WORD:
				if (ch == '(')
					++bracketcnt;
				else if (ch == ')')
					--bracketcnt;
				if (ch == '\n' && bracketcnt  > 0)
					printf(CONT_PROMPT);
				else if (ch == '\n')
				{
					MEMORYOP(endString(&(*params)[*nparams - 1].word, len));
					return RET_OK;
				}
				else if (ch == '\\')
				{
					previous = IN_WORD;
					state = IN_ESCAPE;
				}
				else if (ch == '#')
				{
					flushStdin();
					MEMORYOP(endString(&(*params)[*nparams - 1].word, len));
					return RET_OK;
				}
				else if (ch == '"')
				{
					state = IN_QUOTES;
					MEMORYOP(endString(&(*params)[*nparams - 1].word, len));
					MEMORYOP(addParam(params, nparams, WT_WORD));
					len = 0;
				}
				else if (isspace (ch))
				{
					state = IN_BETWEEN;
					MEMORYOP(endString(&(*params)[*nparams - 1].word, len));
				}
				else if ((type = charType (ch, 0)) > 0)
				{
					state = IN_SPECIAL;
					MEMORYOP(endString(&(*params)[*nparams - 1].word, len));
					len = 0;
					MEMORYOP(addParam(params, nparams, type));
					MEMORYOP(addChar(&(*params)[*nparams - 1].word, &len, ch));
					MEMORYOP(endString(&(*params)[*nparams - 1].word, 1));
				}
				else if (ch == '$')
				{
					state = IN_ENV;
					envlen = 0;
				}
				else
					MEMORYOP(addChar(&(*params)[*nparams - 1].word, &len, ch));
				break;

			case IN_ESCAPE:
				state = previous;
				if (ch == '\n')
					printf(CONT_PROMPT);
				else
					MEMORYOP(addChar(&(*params)[*nparams - 1].word, &len, ch));
				break;

			case IN_QUOTES:
				if (ch == '\n')
					printf(CONT_PROMPT);
				if (ch == '\\')
				{
					previous = IN_QUOTES;
					state = IN_ESCAPE;
				}
				else if (ch == '"')
				{
					state = IN_BETWEEN;
					MEMORYOP(endString(&(*params)[*nparams - 1].word, len));
				}
				else
					MEMORYOP(addChar(&(*params)[*nparams - 1].word, &len, ch));
				break;
		}
	}
}

/* Checks if command is internal that must be executed in the main process */
int isInternal(param_t *params, int nparams)
{
	int i;

	if (params[0].type != WT_WORD || (strcmp(params[0].word, "cd") && strcmp(params[0].word, "exit")
	    && strcmp(params[0].word, "jobs") && strcmp(params[0].word, "fg") && strcmp(params[0].word, "bg")))
		return 0;
	for (i = 1; i < nparams - 1; ++i)
		if (params[i].type == WT_PIPE)
			return 0;

	return 1;
}

/* Shifts process group of pid process to foreground and waits for all the processes
   to finish. Returns 1 if the process was stopped and 0 if it has termeniated */
int waitProcessGroup(pid_t lastpid, int pgid, int *status)
{
	int st, pidstatus = 0, tracemode = 0;
	pid_t pid;
	if (!issubshell)
		tracemode = WUNTRACED;

	if (!issubshell)
		tcsetpgrp(STDIN_FILENO, pgid);
	kill (-pgid, SIGCONT);
	while ((pid = waitpid(-pgid, &st, tracemode)) != (pid_t)-1 && !WIFSTOPPED(st))
		if (pid == lastpid)
			pidstatus = st;
	if (!issubshell)
		tcsetpgrp(STDIN_FILENO, getpid ());

	if (WIFSTOPPED(st))
		putchar('\n');
	if (status != NULL && !WIFSTOPPED(st))
		*status = WEXITSTATUS(pidstatus);

	return WIFSTOPPED(st);
}

/* Finds two given diveders in range from begin to array end */
int findDivider(param_t *params, int nparams, int begin, word_t div1, word_t div2)
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
int launchJobs(param_t *, int, job_t **, int *);

/* Executes a straightforward params (no pipes, no dividers - just params with parameters) */
void executeCommand(param_t *params, int nparams, job_t *jobs, int njobs)
{
	char **command;
	int i;

	signal(SIGINT,  SIG_DFL);
	signal(SIGTSTP, SIG_DFL);
	signal(SIGTTOU, SIG_DFL);

	if (params[0].type == WT_LBRACKET) /* Launching subshell */
	{
		issubshell = 1;
		exit(launchJobs(params + 1, nparams - 2, &jobs, &njobs));
	}

	if (!strcmp(params[0].word, "cd") || !strcmp(params[0].word, "exit")
	    || !strcmp(params[0].word, "fg") || !strcmp(params[0].word, "bg"))
		exit(0);
	if (!strcmp(params[0].word, "jobs"))
	{
		showJobs(jobs, njobs, 1);
		exit(0);
	}
	if (!strcmp(params[0].word, "battlefield"))
	{
		puts("Hi guys, TheWorldsEnd here!");
		exit(0);
	}
	if (!strcmp(params[0].word, "pwd"))
	{
		char *s = getcwd(NULL, 0);
		if (s == NULL)
			fatalError();
		puts(s);
		free(s);
		exit(0);
	}

	if ((command = malloc((nparams + 1) * sizeof(char *))) == NULL)
		fatalError();
	for (i = 0; i < nparams; ++i)
		command[i] = params[i].word;
	command[nparams] = NULL;
	execvp(command[0], command);
	error(-1, errno, command[0]);
}

/* Return new command array without file redirectors */
param_t *removeRedirectors(param_t *params, int nparams, int *count)
{
	int i, bracketcnt = 0, cnt = 0;
	param_t *command;

	if ((command = calloc(nparams, sizeof(param_t))) == NULL)
	{
		nonfatalError(errno, NULL);
		return NULL;
	}

	for (i = 0; i < nparams; ++i)
		if (bracketcnt == 0 && IS_FILEOP(params[i].type))
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

/* Dups files from params to stdin and stdout, return 1 is there was at least one redirection */
int dupFiles(param_t *params, int nparams)
{
	int i, bracketcnt = 0, wasredirection = 0, in = STDIN_FILENO, out = STDOUT_FILENO;

	for (i = nparams - 2; i > 0; --i)
		if (bracketcnt == 0 && IS_FILEOP(params[i].type))
		{
			if (out == STDOUT_FILENO && params[i].type == WT_FILEWRTRUNC)
			{
				if ((out = open(params[i + 1].word, O_WRONLY | O_CREAT | O_TRUNC, 0644)) == -1)
					return nonfatalError(errno, params[i + 1].word);
			}
			else if (out == STDOUT_FILENO && params[i].type == WT_FILEWRAPPEND)
			{
				if ((out = open(params[i + 1].word, O_WRONLY | O_CREAT | O_APPEND, 0644)) == -1)
					return nonfatalError(errno, params[i + 1].word);
			}
			else if (in == STDIN_FILENO && params[i].type == WT_FILERD)
			{
				if ((in = open(params[i + 1].word, O_RDONLY)) == -1)
					return nonfatalError(errno, params[i + 1].word);
			}
			--i;
		}
		else if (params[i].type == WT_LBRACKET)
				--bracketcnt;
		else if (params[i].type == WT_RBRACKET)
				++bracketcnt;

	if (in  != STDIN_FILENO)  { dup2(in, 0);  close(in);  wasredirection = 1; }
	if (out != STDOUT_FILENO) { dup2(out, 1); close(out); wasredirection = 1; }

	return wasredirection;
}

/* cd internal commmand */
int internalChangeDir(param_t *params, int nparams)
{
	char *s;
	if (nparams > 1)
		s = params[1].word;
	else
		s = getenv("HOME");
	if (chdir (s))
		return nonfatalError(errno, s);
	return 0;
}

/* jobs internal command */
int internalJobs(param_t *params, int nparams, job_t **jobs, int *njobs)
{
	if (issubshell)
		return nonfatalError(0, "jobs: no job control");
	checkJobs(jobs, njobs);
	showJobs(*jobs, *njobs, 1);
	deleteDoneJobs(jobs, njobs);
	return 0;
}

/* fg internal command */
int internalForeground(param_t *params, int nparams, job_t **jobs, int *njobs)
{
	int n;
	if (issubshell)
		return nonfatalError(0, "fg: no job control");

	if (nparams == 1)
		n = *njobs - 1;
	else
		n = atoi(params[1].word) - 1;
	if (n < 0 || n >= *njobs)
		return nonfatalError(0, "no such job");

	if (waitProcessGroup(0, (*jobs)[n].pgid, NULL))
		(*jobs)[n].status = ST_JUSTSTP;
	else
		deleteJob(jobs, njobs, n);

	return 0;
}

/* bg internal command */
int internalBackground(param_t *params, int nparams, job_t **jobs, int *njobs)
{
	int n;
	if (issubshell)
		return nonfatalError(0, "bg: no job control");

	if (nparams == 1)
	{
		n = *njobs;
		while (--n >= 0 && (*jobs)[n].status != ST_STOPPED);
	}
	else
		n = atoi(params[1].word) - 1;

	if (n < 0 || n >= *njobs)
		return nonfatalError(0, "no such job");
	kill(-(*jobs)[n].pgid, SIGCONT);
	printf("[%d] %s\n", n + 1, (*jobs)[n].job);

	return 0;
}

/* Executes internal command */
int internalCommand(param_t *params, int nparams, job_t **jobs, int *njobs)
{
	int savestdin, savestdout, count, result, wasredirection;
	param_t *command;
	savestdin =  dup(STDIN_FILENO);
	savestdout = dup(STDOUT_FILENO);

	command = params;
	count = nparams;
	if (((wasredirection = dupFiles(params, nparams)) == -1)
	    || (wasredirection && (command = removeRedirectors(params, nparams, &count)) == NULL))
	{
		dup2(savestdin,  STDIN_FILENO);
		dup2(savestdout, STDOUT_FILENO);
		close(savestdin);
		close(savestdout);
		return -1;
	}

	if (!strcmp(command[0].word, "exit"))
		exit (0);
	else if (!strcmp(command[0].word, "cd"))
		result = internalChangeDir(command, count);
	else if (!strcmp(command[0].word, "jobs"))
		result = internalJobs(command, count, jobs, njobs);
	else if (!strcmp(command[0].word, "fg"))
		result = internalForeground(command, count, jobs, njobs);
	else if (!strcmp(command[0].word, "bg"))
		result = internalBackground(command, count, jobs, njobs);

	if (wasredirection)
		free(command);
	dup2(savestdin,  STDIN_FILENO);
	dup2(savestdout, STDOUT_FILENO);
	close(savestdin);
	close(savestdout);
	return result;
}

/* Organizes i/o redirection. Returns pid of last process in the pipeline, responsible for <, >, >> and | */
pid_t launchCommands(param_t *params, int nparams, job_t *jobs, int njobs)
{
	pid_t pgid = getpgid(0), pid;
	int begin = 0, divider, count, wasredirection, pipes[2][2]={{0}};
	param_t *command;

	while (begin < nparams)
	{
		divider = findDivider(params, nparams, begin, WT_PIPE, WT_PIPE);

		if (begin > 0)
		{
			if (pipes[0][0])
				close(pipes[0][0]);
			close(pipes[1][1]);
			pipes[0][0] = pipes[1][0];
		}
		if (divider < nparams)
			pipe(pipes[1]);

		if ((pid = fork()) == -1)
			return (pid_t)nonfatalError(errno, NULL);
		if (begin == 0 && !issubshell)
			pgid = pid;
		if (!pid)
		{
			setpgid(0, pgid);

			if (begin > 0)
			{
				dup2(pipes[0][0], 0);
				close(pipes[0][0]);
			}
			if (divider < nparams)
			{
				dup2(pipes[1][1], 1);
				close(pipes[1][0]);
				close(pipes[1][1]);
			}

			count = nparams;
			command = params;
			if ((wasredirection = dupFiles(params + begin, divider - begin)) == -1)
				exit(-1);
			if (wasredirection && (command = removeRedirectors(params + begin, divider - begin, &count)) == NULL)
				exit(-1);
			executeCommand(command, count, jobs, njobs);
		}
		setpgid(pid, pgid);
		begin = divider + 1;
	}

	if (pipes[0][0])
		close(pipes[0][0]);
	return pid;
}

/* Executes one job in its own process group, responsible for && and || */
int controlJob(param_t *params, int nparams, int isforeground, job_t **jobs, int *njobs)
{
	int begin = 0, divider, exitstatus = 0;
	pid_t pid, pgid;

	while (begin < nparams)
	{
		divider = findDivider(params, nparams, begin, WT_AND, WT_OR);
		if (begin > 0 && ((params[begin - 1].type == WT_AND && exitstatus) || (params[begin - 1].type == WT_OR && !exitstatus)))
		{
			begin = divider + 1;
			continue;
		}

		if (isforeground && isInternal(params + begin, divider - begin))
		{
			exitstatus = internalCommand(params + begin, divider - begin, jobs, njobs);
			begin = divider + 1;
			continue;
		}

		if ((pid = launchCommands(params + begin, divider - begin, *jobs, *njobs)) == (pid_t)-1)
		{
			exitstatus = -1;
			begin = divider + 1;
			continue;
		}
		pgid = getpgid(pid);
		exitstatus = -1;

		if (isforeground)
		{
			if (waitProcessGroup(pid, pgid, &exitstatus))
				addJob(jobs, njobs, params + begin, divider - begin, pgid, ST_JUSTSTP);
		}
		else if (!issubshell)
			addJob(jobs, njobs, params + begin, divider - begin, pgid, ST_RUNNING);

		begin = divider + 1;
	}

	return exitstatus;
}

/* Launches background and foreground jobs, responsible for ; and & */
int launchJobs(param_t *params, int nparams, job_t **jobs, int *njobs)
{
	int begin = 0, divider, isforeground, needcontrol, exitstatus;
	pid_t pid;

	if (issubshell)
		signal(SIGTTOU, SIG_IGN);

	while (begin < nparams)
	{
		divider = findDivider(params, nparams, begin, WT_BACKGROUND, WT_SEMICOLON);
		isforeground = divider == nparams || params[divider].type == WT_SEMICOLON;
		needcontrol = !isforeground && findDivider(params + begin, divider - begin, 0, WT_AND, WT_OR) < divider - begin;

		if (needcontrol)
		{
			if ((pid = fork()) == -1)
				return nonfatalError(errno, NULL);
			if (!pid)
			{
				setpgid(0, 0);
				issubshell = 1;
				exit(launchJobs(params, nparams - 1, jobs, njobs));
			}
			setpgid(pid, pid);
			addJob(jobs, njobs, params + begin, divider - begin, pid, ST_RUNNING);
		}
		else
			exitstatus = controlJob(params + begin, divider - begin, isforeground, jobs, njobs);

		begin = divider + 1;
	}

	return exitstatus;
}

/* Checks if syntax is correct. Returns 1 on correct syntax */
int checkSyntax(param_t *params, int nparams)
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
			error(0, 0, "syntax error near %s", params[i].word);
		else
			error(0, 0, "unexpected end of file");
		return 0;
	}

	return 1;
}

/* Sets some environmental variables for later use */
int setEnvVars()
{
	char buf[PATH_MAX];
	int len;

	if ((len = readlink("/proc/self/exe", buf, PATH_MAX - 1)) == -1)
		return nonfatalError(errno, NULL);
	buf[len] = '\0';
	if (setenv("SHELL", buf, 1) == -1)
		return nonfatalError(errno, NULL);
	sprintf (buf, "%d", geteuid ());
	if (setenv("EUID", buf, 1) == -1)
		return nonfatalError(errno, NULL);
	if (getlogin_r(buf, PATH_MAX) || setenv("USER", buf, 1) == -1)
		return nonfatalError(errno, NULL);

	return 0;
}

/* Initialize xish */
int doInit(int argc, char **argv)
{
	strcpy(argv[0], "xish");
	if (setEnvVars())
		return -1;
	return 0;
}

/* Show input prompt */
void showPrompt()
{
	char hostname[HOST_NAME_MAX], *cwd = getcwd(NULL, 0);
	if (gethostname(hostname, HOST_NAME_MAX) || cwd == NULL)
	{
		printf(DFL_PROMPT);
		if (cwd != NULL)
			free(cwd);
		return;
	}
	printf("%s@%s %s $ ", getlogin(), hostname, cwd);
	free(cwd);
}

/* Just a main */
int main(int argc, char **argv)
{
	int nparams, njobs = 0;
	param_t *params = NULL;
	job_t *jobs = NULL;
	result_t result;

	signal(SIGINT,  SIG_IGN);
	signal(SIGTSTP, SIG_IGN);
	signal(SIGTTOU, SIG_IGN);

	if (doInit(argc, argv) == -1)
		error(0, 0, "initialisation failed, it is not advised to continue");
	showPrompt();

	while ((result = readCommand(&params, &nparams)) != RET_EOF)
	{
		if (result != RET_MEMORYERR && checkSyntax(params, nparams))
			launchJobs(params, nparams, &jobs, &njobs);
		checkJobs(&jobs, &njobs);
		showJobs(jobs, njobs, 0);
		deleteDoneJobs (&jobs, &njobs);
		showPrompt();
		clearParams(&params, nparams);
	}

	putchar('\n');
	clearParams(&params, nparams);
	clearJobs(&jobs, njobs);

	return 0;
}