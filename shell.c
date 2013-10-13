#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <ctype.h>
#include <sys/types.h>
#include <unistd.h>
#include <sys/wait.h>

#define STR_SIZE 128
#define PARAM_COUNT 4
#define INPUT_CH '>'
#define BUF_SIZE 1024

typedef enum { RET_OK, RET_ERR, RET_EOFOK, RET_EOFERR, RET_MEMORYERR } ret_result_t;

int checkStringLen (char **str, int len)
{
	char *ptr;

	if (len % STR_SIZE)
		return 0;

	if ((ptr = realloc (*str, (len + STR_SIZE) * sizeof (char))) != NULL)
	{
		*str = ptr;
		return 0;
	}

	return 1;
}

int checkParamCnt (char ***params, int nparams)
{
	char **ptr;

	if (nparams % PARAM_COUNT)
		return 0;

	if ((ptr = realloc (*params, (nparams + PARAM_COUNT) * sizeof (char *))) != NULL)
	{
		*params = ptr;
		return 0;
	}

	return 1;
}

int endString (char **params, int nparams, int len)
{
	char *ptr;

	if (len % STR_SIZE != 0)
	{
		params[nparams - 1][len] = '\0';
		return 0;
	}

	if ((ptr = realloc (params[nparams - 1], (len + 1) * sizeof (char))) != NULL)
	{
		params[nparams - 1] = ptr;
		params[nparams - 1][len] = '\0';
		return 0;
	}

	return 1;
}

int addChar (char **str, int *len, char ch)
{
	if (checkStringLen (str, *len))
		return 1;

	(*str)[*len] = ch;
	(*len)++;

	return 0;
}

int addParam (char ***params, int *nparams)
{	
	if (*nparams)
		if (checkParamCnt (params, *nparams + 1))
			return 1;
	if (((*params)[*nparams] = calloc (STR_SIZE, sizeof (char))) != NULL)
	{
		(*nparams)++;
		return 0;
	}

	return 1;
}

ret_result_t readCommand (char ***params, int *nparams)
{
	enum { IN_INITIAL, IN_WORD, IN_BETWEEN, IN_SCREEN, IN_QUOTES, IN_COMMENT, IN_ERROR } state = IN_INITIAL, previous;
	int ch, len = 0;

	*nparams = 0;
	if ((*params = calloc (PARAM_COUNT, sizeof (char *))) == NULL)
		return RET_MEMORYERR;
	
	while (1)
	{
		ch = getchar ();
		switch (state)
		{
			case IN_INITIAL:
				if (ch == '\n' || ch == '#')
				{
					if (addParam (params, nparams))
						return RET_MEMORYERR;
					(*params)[0][0] = '\0';
				}
				
			case IN_BETWEEN:
				if (ch == EOF)
					return RET_EOFOK;
				else if (ch == '\n')
					return RET_OK;
				else if (ch == '\\')
				{
					previous = IN_WORD;
					state = IN_SCREEN;
					if (addParam (params, nparams))
						return RET_MEMORYERR;
					len = 0;
				}
				else if (ch == '#')
					state = IN_COMMENT;
				else if (ch == '"')
				{
					state = IN_QUOTES;
					if (addParam (params, nparams))
						return RET_MEMORYERR;
					len = 0;
				}
				else if (isspace (ch))
					;
				else
				{
					state = IN_WORD;
					if (addParam (params, nparams))
						return RET_MEMORYERR;
					len = 1;
					(*params)[*nparams - 1][0] = ch;
				}
				break;

			case IN_WORD:
				if (ch == EOF)
				{
					if (endString (*params, *nparams, len))
						return RET_MEMORYERR;
					return RET_EOFOK;
				}
				else if (ch == '\n')
				{
					if (endString (*params, *nparams, len))
						return RET_MEMORYERR;
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
					if (endString (*params, *nparams, len))
						return RET_MEMORYERR;
				}
				else if (ch == '"')
					state = IN_ERROR;
				else if (isspace (ch))
				{
					state = IN_BETWEEN;
					if (endString (*params, *nparams, len))
						return RET_MEMORYERR;
				}
				else
					if (addChar (*params + *nparams - 1, &len, ch))
						return RET_MEMORYERR;
				break;

			case IN_SCREEN:
				if (ch == EOF)
					return RET_EOFERR;
				else if (ch == '\n')
					return RET_ERR;
				else if (ch == '\\' || ch == '#' || ch == '"')
				{
					state = previous;
					if (addChar (*params + *nparams - 1, &len, ch))
						return RET_MEMORYERR;
				}
				else
					state = IN_ERROR;
				break;

			case IN_QUOTES:
				if (ch == EOF)
					return RET_EOFERR;
				else if (ch == '\n')
					return RET_ERR;
				else if (ch == '\\')
				{
					previous = IN_QUOTES;
					state = IN_SCREEN;
				}
				else if (ch == '"')
				{
					state = IN_WORD;
					if (endString (*params, *nparams, len))
						return RET_MEMORYERR;
				}
				else
					if (addChar (*params + *nparams - 1, &len, ch))
						return RET_MEMORYERR;
				break;

			case IN_COMMENT:
				if (ch == EOF)
					return RET_EOFOK;
				if (ch == '\n')
					return RET_OK;
				break;

			case IN_ERROR:
				if (ch == EOF)
					return RET_EOFERR;
				if (ch == '\n')
					return RET_ERR;
				break;
		}
	}
}

void clearStrings (char ***params, int nparams)
{
	int i;

	if (params == NULL)
		return;

	for (i = 0; i < nparams; i++)
		free ((*params)[i]);
	free (*params);

	*params = NULL;
}

int placeEnv (char **params, int nparams)
{
	int i;
	for (i = 0; i < nparams; i++)
	{
		int len = strlen (params[i]), posp = 0, poss = 0, ch;
		char *s;

		if (len < 2 || !strchr (params[i], '$'))
			continue;
		
		if ((s = calloc (STR_SIZE, sizeof (char))) == NULL)
			return 1;

		while (posp < len)
		{
			char *env;
			int start;

			while (posp < len && params[i][posp] != '$')
				if (checkStringLen (&s, poss))
					return 1;
				else
					s[poss++] = params[i][posp++];
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
						return 1;
					s = ptr;
				}
				strcpy (s + poss, env);
				poss += strlen (env);
			}
			params[i][posp] = ch;
		}

		if (checkStringLen (&s, poss))
			return 1;
		s[poss] = '\0';
		free (params[i]);
		params[i] = s;
	}
	return 0;
}

int executeCommand (char **params, int nparams)
{
	pid_t pid;

	if (!nparams || params[0][0] == '\0')
		return 0;
	
	if (placeEnv (params, nparams))
		return 0;

	if (!strcmp (params[0], "pwd"))
	{
		char *s = getcwd (NULL, 0);
		if (s == NULL)
		{
			perror ("Error getting current directory ");
			return 0;
		}
		puts (s);
		free (s);
		return 0;
	}
	if (!strcmp (params[0], "cd"))
	{
		if (nparams == 1)
		{
			if (chdir (getenv ("HOME")))
				perror ("Error changing directory ");
		}
		else 
			if (chdir (params[1]))
				perror ("Error changing directory ");
		return 0;
	}
	if (!strcmp (params[0], "exit"))
		return 1;

	if ((pid = fork ()) < 0)
		perror ("Error creating child process ");
	else if (pid)
		wait (NULL);
	else
	{
		signal (SIGINT, SIG_DFL);
		params[nparams] = NULL;
		execvp (params[0], params);
		perror ("Error executing command ");
		exit (0);
	}

	return 0;
}

int setEnvVars ()
{
	char buf[BUF_SIZE];
	int len;

	if ((len = readlink ("/proc/self/exe", buf, BUF_SIZE - 1)) == -1)
	{
		perror ("Error getting program name ");
		return 1;
	}
	buf[len] = '\0';
	if (setenv ("SHELL", buf, 1) == -1)
	{
		perror ("Error setting program name ");
		return 1;
	}

	sprintf (buf, "%d", geteuid ());
	if (setenv ("EUID", buf, 1) == -1)
	{
		perror ("Error setting puid ");
		return 1;
	}

	return 0;
}

int main ()
{
	int nparams;
	char **params = NULL;
	ret_result_t ret;

	signal (SIGINT, SIG_IGN);
	if (setEnvVars ())
		return 1;

	putchar (INPUT_CH);

	while ((ret = readCommand (&params, &nparams)) < RET_EOFOK)
	{
		if (ret == RET_ERR)
			puts ("Wrong format!");
		else
			if (executeCommand (params, nparams))
				break;

		putchar (INPUT_CH);
		clearStrings (&params, nparams);
	}
	
	if (ret == RET_EOFOK)
		executeCommand (params, nparams);
	else if (ret == RET_MEMORYERR)
	{
		putchar ('\n');
		puts ("Memory allocation error!");
	}
	else if (ret == RET_EOFERR)
	{
		putchar ('\n');
		puts ("Wrong format!");
	}
	clearStrings (&params, nparams);

	return 0;
}
