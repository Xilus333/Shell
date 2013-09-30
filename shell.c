#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <ctype.h>

#define STR_SIZE 10
#define PARAM_COUNT 3
#define INPUT_CH '>'


typedef enum { retOK, retErr, retEOFOK, retEOFErr } ret_result_t;

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
	enum { inInitial, inWord, inBetween, inScreen, inQuotes, inComment, inError } state = inInitial;
	char ch;
	int len = 0;

	*nparams = 0;
	*params = calloc (PARAM_COUNT, sizeof (char *));
	
	for (;;)
	{
		ch = getchar ();
		switch (state)
		{
			case inInitial:
				if (ch == '\n' || ch == '#')
				{
					(*nparams)++;
					(*params)[0] = calloc (STR_SIZE, sizeof (char));
					(*params)[0][0] = '\0';
				}
				
			case inBetween:
				if (ch == EOF)
					return retEOFOK;
				else if (ch == '\n')
					return retOK;
				else if (ch == '\\')
				{
					state = inScreen;
					(*nparams)++;
					checkParamCnt (params, *nparams);
					(*params)[*nparams - 1] = calloc (STR_SIZE, sizeof (char));
					len = 0;
				}
				else if (ch == '#')
					state = inComment;
				else if (ch == '"')
				{
					state = inQuotes;
					(*nparams)++;
					checkParamCnt (params, *nparams);
					(*params)[*nparams - 1] = calloc (STR_SIZE, sizeof (char));
					len = 0;
				}
				else if (isspace (ch))
					;
				else
				{
					state = inWord;
					(*nparams)++;
					checkParamCnt (params, *nparams);
					(*params)[*nparams - 1] = calloc (STR_SIZE, sizeof (char));
					len = 1;
					(*params)[*nparams - 1][0] = ch;
				}
				break;

			case inWord:
				if (ch == EOF)
				{
					endString (*params, *nparams, len);
					return retEOFOK;
				}
				else if (ch == '\n')
				{
					endString (*params, *nparams, len);
					return retOK;
				}
				else if (ch == '\\')
					state = inScreen;
				else if (ch == '#')
				{
					state = inComment;
					endString (*params, *nparams, len);
				}
				else if (ch == '"')
					state = inError;
				else if (isspace (ch))
				{
					state = inBetween;
					endString (*params, *nparams, len);
				}
				else
				{
					checkStringLen (*params + *nparams - 1, len + 1);
					(*params)[*nparams - 1][len] = ch;
					len++;
				}
				break;

			case inScreen:
				if (ch == EOF)
					return retEOFErr;
				else if (ch == '\\' || ch == '#' || ch == '"')
				{
					state = inWord;
					checkStringLen (*params + *nparams - 1, len + 1);
					(*params)[*nparams - 1][len] = ch;
					len++;
				}
				else
					state = inError;
				break;

			case inQuotes:
				if (ch == EOF)
					return retEOFErr;
				else if (ch == '\n')
					state = inError;
				else if (ch == '"')
				{
					state = inBetween;
					endString (*params, *nparams, len);
				}
				else
				{
					checkStringLen (*params + *nparams - 1, len + 1);
					(*params)[*nparams - 1][len] = ch;
					len++;
				}
				break;

			case inComment:
				if (ch == EOF)
					return retEOFOK;
				if (ch == '\n')
					return retOK;
				break;

			case inError:
				if (ch == EOF)
					return retEOFErr;
				if (ch == '\n')
					return retErr;
				break;
		}
	}

	return retErr;
}

void clearStrings (char ***params, int nparams)
{
	int i;

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

	while ( (ret = readCommand (&params, &nparams)) != retEOFOK && ret != retEOFErr)
	{
		if (ret == retErr)
			puts ("Wrong format!");
		else
			for (i = 0; i < nparams; i++)
				puts (params[i]);
		putchar (INPUT_CH);
		clearStrings (&params, nparams);
	}
	
	if (ret == retEOFOK)
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
