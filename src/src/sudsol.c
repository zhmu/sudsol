#include <err.h>
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

/* dimensions */
#define SIZE_X 9
#define SIZE_Y 9
#define PART_SIZE_X 3
#define PART_SIZE_Y 3

#define HORIZONTAL 0
#define VERTICAL 1

/* Define this for debugging messages */
#undef DEBUG

#ifdef DEBUG
	#define DPRINTF printf
#else
	#define DPRINTF(...)
#endif

typedef uint32_t number_t;
/* bit 1 is never used, hence the -2 */
#define ANY_NUMBER ((1<<(SIZE_X+1))-2)

/*\! brief Maximum number of backsteps we can do */
#define MAX_BACKLOG (99)

/*! \brief Playfield itself */
number_t playfield[SIZE_X * SIZE_Y];

/*! \brief Helper macro to deal with the playfield */
#define PLAYFIELD(x,y) (playfield[(y)*SIZE_X+(x)])

/*! \brief Helper structure for backtracking */
struct BACKLOG {
	/*! \brief Original playfield */
	number_t playfield[SIZE_X * SIZE_Y];

	/*! \brief Number to be tried */
	int number;

	/*! \brief Instance to be tried */
	int instance;

	/*! \brief Direction to try */
	int direction;
};

struct BACKLOG* backlog;
int cur_backlog = 0;

/*! \brief Loads a playing field
 *  \param fname File to load from
 *  \return Non-zero on success, zero on failure
 */
int
load_playfield(char* fname)
{
	FILE* f;
	char line[80 /* XXX */ ];
	char* ptr;
	int x, y;

	if ((f = fopen (fname, "rt")) == NULL) {
		warn("can't open file");
		return 0;
	}

	/* Everything can be everything */
	for (y = 0; y < SIZE_Y; y++)
		for (x = 0; x < SIZE_X; x++)
			PLAYFIELD(x, y) = ANY_NUMBER;

	y = 0;
	while (fgets (line, sizeof (line), f)) {
		while ((ptr = strchr (line, '\n')) != NULL)
			*ptr = 0;

		if (y >= SIZE_Y) {
			fprintf(stderr, "too much lines in datafile (reached line %u, compiled for %u)\n", y + 1, SIZE_Y);
			return 0;
		}

		if (strlen (line) != SIZE_X) {
			fprintf(stderr, "invalid line %u column count (%u != size %d)\n", y + 1, x, SIZE_X);
			return 0;
		}

		x = 0;
		for (ptr = line; *ptr != 0; ptr++, x++) {
			if (*ptr == '.') continue;
			PLAYFIELD(x, y) = 1 << (*ptr - '0');
		}
		y++;
	}

	fclose (f);
	return 1;
}

/*! \brief Dump of the playfield
 */
void
dump_playfield() {
	int x, y;
	int i;

	for (y = 0; y < SIZE_Y; y++) {
		for (x = 0; x < SIZE_X; x++) {
			printf("<%03d>", PLAYFIELD(x, y));
			for (i = 1; i <= SIZE_X; i++)
				printf ("%c", (PLAYFIELD(x, y) & (1 << i)) ? i + '0' : '.');
			printf ("%c", (x == 2) || (x == 5) ? '|' : ' ');
		}
		printf ("\n");
		if ((y == 2) || (y == 5)) {
			for (i = 0; i < (SIZE_X * SIZE_X); i++)
				printf ("-");
			printf ("\n");
		}
	}
}

/*! \brief Dumps our possible solved playfield
 */
void
dump_solved_playfield()
{
	int x, y;
	int i, onesol;

	for (y = 0; y < SIZE_Y; y++) {
		for (x = 0; x < SIZE_X; x++) {
			onesol = -1;
			for (i = 1; i <= SIZE_X; i++)
				if (PLAYFIELD(x, y) == (1 << i))
					onesol = i;
			printf ("%c", (onesol != -1) ? onesol + '0' : ' ');
			printf ("%s", (x == 2) || (x == 5) ? "|" : "");
		}
		printf ("\n");
		if ((y == 2) || (y == 5)) {
			for (i = 0; i < (SIZE_X * 2); i++)
				printf ("-");
			printf ("\n");
		}
	}
}

/*! \brief Tries to locate a number
 *  \param num The number to locate
 *  \param x Destination X offset
 *  \param y Destination Y offset
 *  \param instance Instance to locate
 *  \return Non-zero on success, zero on failure
 *
 *  This will only return values which contain ONLY num.
 */
int
locate_num (int num, int* x, int* y, int instance)
{
	for (*y = 0; *y < SIZE_Y; (*y)++)
		for (*x = 0; *x < SIZE_X; (*x)++)
			if (PLAYFIELD(*x, *y) == (1 << num))
				if (!instance--)
					return 1;

	return 0;
}

/*! \brief Checks whether the valid is a solid number
 *  \param val The value to check
 *  \return Zero if it is not, non-zero if it is
 *
 *  A solid number is a value which represents exactly one possible
 *  number, not a possible combination of numbers.
 */
int
check_solid (int val)
{
	int i;

	for (i = 1; i <= SIZE_X; i++) {
		if (val == (1 << i))
			return 1;
	}
	
	return 0;
}

/*! \brief Preprocesses the playfield
 *  \param warn Produce warnings on invalid content
 *  \return The number of changes made or -1 on failure
 *
 *  Step 1 will perform simple removal of number which can never occur.  It
 *  works by locating each number in turn ( 1 .. SIZE_X ) and then removing
 *  this number horizonally and vertically.
 *
 *  Step 2 will do the same removal, but yet limited to a part of the puzzle
 *  (for example 9x9 is divided in 3x3 parts).
 *
 *  This function should be called until it returns 0. Once it is
 *  completed, more intelligent analysis must be done as multiple
 *  combinations are possible.
 */
int
preprocess(int warn)
{
	int step, instance;
	int first_x, first_y;
	int part_x, part_y;
	int x, y, numchanges = 0;
	int num, mask;

	/* step 1: full puzzle */
	for (step = 1; step <= SIZE_X; step++) {
		instance = 0;
		while (locate_num (step, &first_x, &first_y, instance++)) {
			/* Scan the columns vertically and remove this value as
			 * possibility for the numbers */
			for (y = 0; y < SIZE_Y; y++) {
				if (y == first_y) continue;
				if (PLAYFIELD(first_x, y) == (1 << step)) {
					if (warn) fprintf(stderr, "preprocess(): number %u is found at (%u,%u) but also at (%u,%u)?\n", step, first_x, first_y, first_x, y);
					return -1;
				}
				if (PLAYFIELD(first_x, y) & (1 << step)) {
					PLAYFIELD(first_x, y) &= ~(1 << step);
					numchanges++;
				}
			}

			/* Scan the rows horizontally and remove this value as
			 * possibility for the numbers */
			for (x = 0; x < SIZE_X; x++) {
				if (x == first_x) continue;
				if (PLAYFIELD(x, first_y) == (1 << step)) {
					if (warn) fprintf(stderr, "preprocess(): number %u is found at (%u,%u) but also at (%u,%u)?\n", step, first_x, first_y, x, first_y);
					return -1;
				}
				if (PLAYFIELD(x, first_y) & (1 << step)) {
					PLAYFIELD(x, first_y) &= ~(1 << step);
					numchanges++;
				}
			}
		}
	}

	/* step 2: subpuzzle */
	for (part_y = 0; part_y < SIZE_Y; part_y += PART_SIZE_Y) {
		for (part_x = 0; part_x < SIZE_X; part_x += PART_SIZE_X) {
			/* Calculate the mask of numbers which may never occur */
			mask = 0;
		  for (y = 0; y < PART_SIZE_Y; y++)
			  for (x = 0; x < PART_SIZE_X; x++)
					for (num = 1; num <= SIZE_X; num++)
						if (PLAYFIELD(part_x + x, part_y + y) == (1 << num))
							mask |= (1 << num);

			/* Remove the mask */
		  for (y = 0; y < PART_SIZE_Y; y++)
			  for (x = 0; x < PART_SIZE_X; x++) {
					if ((!check_solid(PLAYFIELD(part_x + x, part_y + y)) && PLAYFIELD(part_x + x, part_y + y) & mask)) {
						PLAYFIELD(part_x + x, part_y + y) &= ~mask;
						numchanges++;
					}
				}
		}
	}

	return numchanges;
}

/*! \brief Postprocesses the puzzle
 *  \return The number of changes made or -1 on failure
 *
 *  Tries to postprocess the puzzle by filling up parts (this is, a
 *  9x9 puzzle is divided in 3x3 parts). Each part must have the
 *  numbers [ 1 .. SIZE_X ]. This function checks for only one missing
 *  number and fills it out as needed.
 */
int
postprocess(int warn)
{
	int part_x, part_y, x, y, num;
	int empty_x = 0, empty_y = 0;
	int curmask, gotnum, numchanges = 0;

	for (part_y = 0; part_y < SIZE_Y; part_y += PART_SIZE_Y) {
		for (part_x = 0; part_x < SIZE_X; part_x += PART_SIZE_X) {
			/*
			 * Check for this part a combination of [ 1 .. PART_X ]
			 */
			curmask = ANY_NUMBER;
		  for (y = 0; y < PART_SIZE_Y; y++)
			  for (x = 0; x < PART_SIZE_X; x++) {
					gotnum = 0;
					for (num = 1; num <= SIZE_X; num++) {
							if (PLAYFIELD(part_x + x, part_y + y) == (1 << num)) {
								curmask &= ~(1 << num); gotnum++;
							}
						}
						if (!gotnum) {
							/* If we never found a solid number here, this is an open
							 * spot. It doesn't matter if we accidently overwrite this
							 * value, as it will only be used if there is exactly one
							 * available number.
							 */
							empty_x = x; empty_y = y;
						}
					}

			/* Check if only a solid number remains */
			for (num = 1; num <= SIZE_X; num++)
				if (curmask == (1 << num)) {
					/* Got it! Put it into place */
					PLAYFIELD(part_x + empty_x, part_y + empty_y) = (1 << num);

					/* Re-run the preprocesser; this removes references to this
					 * value which we now know can't occur */
					if (preprocess(warn) == -1)
						return -1;
					numchanges++;
				}
		}
	}

	return numchanges;
}

/*! \brief Checks whether the puzzle is solved
 *  \return Zero if unsolved, non-zero if it is
 */
int
check_solved() {
	int x, y, mask, step;

	/* horizontal check */
	for (y = 0; y < SIZE_Y; y++) {
		mask = ANY_NUMBER;
		for (step = 1; step <= SIZE_X; step++)
			for (x = 0; x < SIZE_X; x++) 
				if (check_solid (PLAYFIELD (x, y)) && PLAYFIELD(x, y) == (1 << step))
					mask &= ~(1 << step);
		if (mask)
			return 0;
	}

	/* vertical check */
	for (x = 0; x < SIZE_X; x++) {
		mask = ANY_NUMBER;
		for (step = 1; step <= SIZE_X; step++)
			for (y = 0; y < SIZE_Y; y++) 
				if (check_solid (PLAYFIELD (x, y)) && PLAYFIELD(x, y) == (1 << step))
					mask &= ~(1 << step);
		if (mask)
			return 0;
	}

	return 1;
}

/*! \brief Handles marking a specific instance of a number
 *  \param number The number to mark
 *  \param instance Instance of the number
 *  \param dir Direction to use
 *  \return Zero on succes, -1 if the new state is unsolvable, anything else if instance is invalid
 */
int
mark_number (int number, int instance, int dir, int x, int y)
{
	int pp;

	if (dir == HORIZONTAL) {
			for (x = 0; x < SIZE_X; x++)
				if (!check_solid (PLAYFIELD(x, y)) && PLAYFIELD(x, y) & (1 << number))
					if (!instance--)
						/* Found the number. Off it goes! */
						goto found;
	} else {
			for (y = 0; y < SIZE_Y; y++)
				if (!check_solid (PLAYFIELD(x, y)) && PLAYFIELD(x, y) & (1 << number))
					if (!instance--)
						/* Found the number. Off it goes! */
						goto found;
	}

	return 0;

found:
	PLAYFIELD(x, y) = (1 << number);
	do {
		pp = preprocess(0);
		if (pp < 0) {
			return -1;
		}
	} while (pp);
	do {
		pp = postprocess(0);
		if (pp < 0)
			return -1;
	} while (pp);
	return 1;
}

/*! \brief Tries to solve the resulting puzzle
 *  \return Zero if the puzzle is solved, non-zero if we must be called again
 *
 *  This function will try to completely solve the puzzle by searching
 *  both horizontally/vertically for the minimal possible instances of
 *  a number (ie: if the number 2 may appear on row 2 and on row 6 but
 *  not on any other row, it has a weight of 2). It will then sequentially
 *  try combinations out, reverting if the puzzle is grinding to a halt.
 */
int
solver()
{
  int x, y, cur_x = 0, cur_y = 0, dir = 0;
	int cur_less = SIZE_X, cur_num = 0, cur, step;
	int instance, pp;

  /*
   * Find a slot with as little possibilities as possible. This helps us
   * narrow down our search (as other possibilities will wither down
   * once we try this)
   */

	/* first check horizontally */
	for (y = 0; y < SIZE_Y; y++) {
		for (step = 1; step <= SIZE_X; step++) {
			cur = 0;
			for (x = 0; x < SIZE_X; x++) {
				if (!check_solid (PLAYFIELD(x, y)) && PLAYFIELD(x, y) & (1 << step)) {
					cur++;
				}
			}
			if ((cur < cur_less) && cur) {
				cur_less = cur; cur_num = step; cur_x = x; cur_y = y;
				dir = HORIZONTAL;
			}
		}
	}

	/*
	 * If we have found a number which occurs only _once_, it's safe to flag to just use it.
	 */
	if (cur_less == 1) {
		/* locate the number */
		for (cur_x = 0; cur_x < SIZE_X; cur_x++)
			if (!check_solid (PLAYFIELD(cur_x, cur_y)) && PLAYFIELD(cur_x, cur_y) & (1 << cur_num))
				break;

flag:
		/* flag it off */
		PLAYFIELD(cur_x, cur_y) = (1 << cur_num);
		do {
			pp = preprocess(0);
			if (pp == -1)
				goto try_backlog;
		} while (pp > 0);
		while (postprocess(1) > 0);

		/* more work can be done */
		return 1;
	}

	/* check vertically */
	for (x = 0; x < SIZE_X; x++) {
		for (step = 1; step <= SIZE_X; step++) {
			cur = 0;
			for (y = 0; y < SIZE_Y; y++) {
				if (!check_solid (PLAYFIELD(x, y)) && PLAYFIELD(x, y) & (1 << step)) {
					cur++;
				}
			}
			if ((cur < cur_less) && cur) {
				cur_less = cur; cur_num = step; cur_x = x; cur_y = y;
				dir = VERTICAL;
			}
		}
	}
	/*
	 * If we have found a number which occurs only _once_, it's safe to flag to just use it.
	 */
	if (cur_less == 1) {
		/* locate the number */
		for (cur_y = 0; cur_y < SIZE_Y; cur_y++)
			if (!check_solid (PLAYFIELD(cur_x, cur_y)) && PLAYFIELD(cur_x, cur_y) & (1 << cur_num))
				break;

		/* flag it off */
		goto flag;
	}

	/* Don't try to solve already solved puzzles */
	if (cur_less == SIZE_X)
		return 0;

	/*
	 * The choice is made. Now, mark the backlog and get on with it.
	 */
	memcpy (backlog[cur_backlog].playfield, playfield, sizeof (number_t) * SIZE_X * SIZE_Y);
	backlog[cur_backlog].number = cur_num;
	backlog[cur_backlog].instance = 0;
	backlog[cur_backlog].direction = dir;
	if (++cur_backlog == MAX_BACKLOG) {
		fprintf (stderr, "Out of backlog entries :(. Increase MAX_BACKLOG and recompile\n");
		return 0;
	}

	DPRINTF("(%u,%u):%u=>%u\n", cur_x, cur_y, cur_less, cur_num);
	instance = 0;

try_mark_number:
	switch (mark_number (cur_num, instance, dir, cur_x, cur_y)) {
		case -1:
			/*
			 * If this is reached, one of our previous steps is known to be invalid
			 * (as we now are in a state in which the puzzle cannot be solved). We
			 * must revert our previous step.
			 */
			if (cur_backlog == 0) {
				fprintf (stderr, "Unsolvable puzzle during startup! Corrupt? Bug?\n");
				return 0;
			}

try_backlog:
			memcpy (playfield, backlog[cur_backlog - 1].playfield, sizeof (number_t) * SIZE_X * SIZE_Y);

			cur_num = backlog[cur_backlog - 1].number;
			dir = backlog[cur_backlog - 1].direction;
			instance = ++backlog[cur_backlog - 1].instance;
			goto try_mark_number;

	case 0:
			/*
			 * This means our current backlog record options are exhausted. Therefore, our parent backlog
			 * number is faulty and should be redone.
			 */
		  cur_backlog--;
		  if ((cur_backlog == -1) || (cur_backlog == 0)) {
				fprintf (stderr, "Backlog invalid but no backlog?\n");
				return 0;
			}
			goto try_backlog;
	}

	return 1;
}

/*! \brief The main program
 *  \param argc Argument count
 *  \param argv Arguments
 *  \return EXIT_SUCCESS on success, otherwise EXIT_FAILURE
 */
int
main(int argc, char* argv[])
{
	if (argc != 2) {
		fprintf (stderr, "usage: sudsol puzzle.txt\n");
		return EXIT_FAILURE;
	}

	if (!load_playfield(argv[1]))
		return EXIT_FAILURE;

	backlog = malloc (sizeof (struct BACKLOG) * MAX_BACKLOG);
	if (backlog == NULL) {
		fprintf (stderr, "out of memory while creating backlog\n");
		return EXIT_FAILURE;
	}
	memset (backlog, 0, sizeof (struct BACKLOG) * MAX_BACKLOG);

	while (preprocess(1) > 0);
	while (postprocess(1) > 0);
	while (solver() > 0);

	dump_playfield();
	printf("\n");
	dump_solved_playfield();

	printf("\nPuzzle is %ssolved\n", !check_solved() ? "NOT " : "");
	if (!check_solved())
		printf ("This program *SHOULD* be able to solve anything! Therefore, please debug and fix...\n");

	return EXIT_SUCCESS;
}

/* vim:set ts=2 sw=2: */
