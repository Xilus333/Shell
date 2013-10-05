#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <ctype.h>

#define STR_SIZE 128
#define PARAM_COUNT 4
#define INPUT_CH '>'

typedef enum { RET_OK, RET_ERR, RET_EOFOK, RET_EOFERR, RET_MEMORYERR } ret_result_t;

int checkStringLen (char **str, int len)
{
	char *ptr;

	if (len % STR_SIZE)
		return 0;

	if ((ptr = realloc (*str, (len + STR_SIZE) * sizeof (char))))
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

	if ((ptr = realloc (*params, (nparams + PARAM_COUNT) * sizeof (char *))))
	{
		*params = ptr;
		return 0;
	}

	return 1;
}

int endString (char **params, int nparams, int len)
{
	char * ptr;

	if (len % STR_SIZE != 0)
	{
		params[nparams - 1][len] = '\0';
		return 0;
	}

	if ((ptr = realloc (params[nparams - 1], (len + 1) * sizeof (char))))
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
		if (checkParamCnt (params, *nparams))
			return 1;
	if (((*params)[*nparams] = calloc (STR_SIZE, sizeof (char))))
	{
		(*nparams)++;
		return 0;
	}

	return 1;
}

ret_result_t readCommand (char ***params, int *nparams)
{
	enum { IN_INITIAL, IN_WORD, IN_BETWEEN, IN_SCREEN, IN_QUOTES, IN_COMMENT, IN_ERROR } state = IN_INITIAL, previous;
	char ch;
	int len = 0;

	*nparams = 0;
	if (!(*params = calloc (PARAM_COUNT, sizeof (char *))))
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

void executeCommand (char **params, int nparams)
{
	int i;

	for (i = 0; i < nparams; i++)
		puts (params[i]);
}

int main ()
{
	int nparams;
	char **params = NULL;
	ret_result_t ret;

	putchar (INPUT_CH);

	while ((ret = readCommand (&params, &nparams)) < RET_EOFOK)
	{
		if (ret == RET_ERR)
			puts ("Wrong format!");
		else
			executeCommand (params, nparams);

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
	else
	{
		putchar ('\n');
		puts ("Wrong format!");
	}
	clearStrings (&params, nparams);

	return 0;
} 
