#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <ctype.h>

#define STR_SIZE 10
#define PARAM_COUNT 3
#define INPUT_CH '>'


typedef enum { RET_OK, RET_ERR, RET_EOF_OK, RET_EOF_ERR } ret_result_t;

void checkStringLen (char **str, int len)
{
	if (len % STR_SIZE)
		return;
	*str = realloc (*str, (len + STR_SIZE) * sizeof (char));
}

void checkParamCnt (char ***params, int nparams)
{
	if (nparams % PARAM_COUNT)
		return;
	*params = realloc (*params, (nparams + PARAM_COUNT) * sizeof (char *));
}

void endString (char **params, int nparams, int len)
{
	if (len % STR_SIZE == 0)
		params[nparams - 1] = realloc (params[nparams - 1], (len + 1) * sizeof (char));
	params[nparams - 1][len] = '\0';

}

ret_result_t readCommand (char ***params, int *nparams)
{
	enum { IN_INITIAL, IN_WORD, IN_BETWEEN, IN_SCREEN, IN_QUOTES, IN_COMMENT, IN_ERROR } state = IN_INITIAL, previous;
	char ch;
	int len = 0;

	*nparams = 0;
	*params = calloc (PARAM_COUNT, sizeof (char *));
	
	for (;;)
	{
		ch = getchar ();
		switch (state)
		{
			case IN_INITIAL:
				if (ch == '\n' || ch == '#')
				{
					(*nparams)++;
					(*params)[0] = calloc (STR_SIZE, sizeof (char));
					(*params)[0][0] = '\0';
				}
				
			case IN_BETWEEN:
				if (ch == EOF)
					return RET_EOF_OK;
				else if (ch == '\n')
					return RET_OK;
				else if (ch == '\\')
				{
					previous = IN_WORD;
					state = IN_SCREEN;
					(*nparams)++;
					checkParamCnt (params, *nparams);
					(*params)[*nparams - 1] = calloc (STR_SIZE, sizeof (char));
					len = 0;
				}
				else if (ch == '#')
					state = IN_COMMENT;
				else if (ch == '"')
				{
					state = IN_QUOTES;
					(*nparams)++;
					checkParamCnt (params, *nparams);
					(*params)[*nparams - 1] = calloc (STR_SIZE, sizeof (char));
					len = 0;
				}
				else if (isspace (ch))
					;
				else
				{
					state = IN_WORD;
					(*nparams)++;
					checkParamCnt (params, *nparams);
					(*params)[*nparams - 1] = calloc (STR_SIZE, sizeof (char));
					len = 1;
					(*params)[*nparams - 1][0] = ch;
				}
				break;

			case IN_WORD:
				if (ch == EOF)
				{
					endString (*params, *nparams, len);
					return RET_EOF_OK;
				}
				else if (ch == '\n')
				{
					endString (*params, *nparams, len);
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
					endString (*params, *nparams, len);
				}
				else if (ch == '"')
					state = IN_ERROR;
				else if (isspace (ch))
				{
					state = IN_BETWEEN;
					endString (*params, *nparams, len);
				}
				else
				{
					checkStringLen (*params + *nparams - 1, len + 1);
					(*params)[*nparams - 1][len] = ch;
					len++;
				}
				break;

			case IN_SCREEN:
				if (ch == EOF)
					return RET_EOF_ERR;
				else if (ch == '\n')
					return RET_ERR;
				else if (ch == '\\' || ch == '#' || ch == '"')
				{
					state = previous;
					checkStringLen (*params + *nparams - 1, len + 1);
					(*params)[*nparams - 1][len] = ch;
					len++;
				}
				else
					state = IN_ERROR;
				break;

			case IN_QUOTES:
				if (ch == EOF)
					return RET_EOF_ERR;
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
					endString (*params, *nparams, len);
				}
				else
				{
					checkStringLen (*params + *nparams - 1, len + 1);
					(*params)[*nparams - 1][len] = ch;
					len++;
				}
				break;

			case IN_COMMENT:
				if (ch == EOF)
					return RET_EOF_OK;
				if (ch == '\n')
					return RET_OK;
				break;

			case IN_ERROR:
				if (ch == EOF)
					return RET_EOF_ERR;
				if (ch == '\n')
					return RET_ERR;
				break;
		}
	}

	return RET_ERR;
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

int main ()
{
	int nparams, i;
	char **params = NULL;
	ret_result_t ret;

	putchar (INPUT_CH);

	while ( (ret = readCommand (&params, &nparams)) != RET_EOF_OK && ret != RET_EOF_ERR)
	{
		if (ret == RET_ERR)
			puts ("Wrong format!");
		else
			for (i = 0; i < nparams; i++)
				puts (params[i]);
		putchar (INPUT_CH);
		clearStrings (&params, nparams);
	}
	
	if (ret == RET_EOF_OK)
		for (i = 0; i < nparams; i++)
			puts (params[i]);
	else
	{
		putchar ('\n');
		puts ("Wrong format!");
	}
	clearStrings (&params, nparams);

	return 0;
} 
