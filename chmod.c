#include "rsync.h"

extern int orig_umask;

#define FLAG_X_KEEP (1<<0)
#define FLAG_DIRS_ONLY (1<<1)
#define FLAG_FILES_ONLY (1<<2)

struct chmod_mode_struct {
	struct chmod_mode_struct *next;
	int ModeAND, ModeOR;
	char flags;
};

#define CHMOD_ADD 1
#define CHMOD_SUB 2
#define CHMOD_EQ  3

#define STATE_ERROR 0
#define STATE_1ST_HALF 1
#define STATE_2ND_HALF 2

/* Parse a chmod-style argument, and break it down into one or more AND/OR
 * pairs in a linked list.  We use a state machine to walk through the
 * options. */
struct chmod_mode_struct *parse_chmod(const char *modestr,
				      struct chmod_mode_struct *append_to)
{
	int state = STATE_1ST_HALF;
	int where = 0, what = 0, op = 0, topbits = 0, topoct = 0, flags = 0;
	struct chmod_mode_struct *first_mode = NULL, *curr_mode = NULL,
				 *prev_mode = NULL;

	while (state != STATE_ERROR) {
		if (!*modestr || *modestr == ',') {
			int bits;

			if (!op) {
				state = STATE_ERROR;
				break;
			}
			prev_mode = curr_mode;
			curr_mode = new_array(struct chmod_mode_struct, 1);
			if (prev_mode)
				prev_mode->next = curr_mode;
			else
				first_mode = curr_mode;
			curr_mode->next = NULL;

			if (where)
				bits = where * what;
			else {
				where = 0111;
				bits = (where * what) & ~orig_umask;
			}

			switch (op) {
			case CHMOD_ADD:
				curr_mode->ModeAND = 07777;
				curr_mode->ModeOR  = bits + topoct;
				break;
			case CHMOD_SUB:
				curr_mode->ModeAND = 07777 - bits - topoct;
				curr_mode->ModeOR  = 0;
				break;
			case CHMOD_EQ:
				curr_mode->ModeAND = 07777 - (where * 7) - (topoct ? topbits : 0);
				curr_mode->ModeOR  = bits + topoct;
				break;
			}

			curr_mode->flags = flags;

			if (!*modestr)
				break;
			modestr++;

			state = STATE_1ST_HALF;
			where = what = op = topoct = topbits = flags = 0;
		}

		if (state != STATE_2ND_HALF) {
			switch (*modestr) {
			case 'D':
				if (flags & FLAG_FILES_ONLY)
					state = STATE_ERROR;
			  	flags |= FLAG_DIRS_ONLY;
				break;
			case 'F':
				if (flags & FLAG_DIRS_ONLY)
					state = STATE_ERROR;
			  	flags |= FLAG_FILES_ONLY;
				break;
			case 'u':
				where |= 0100;
				topbits |= 04000;
				break;
			case 'g':
				where |= 0010;
				topbits |= 02000;
				break;
			case 'o':
				where |= 0001;
				break;
			case 'a':
				where |= 0111;
				break;
			case '+':
				op = CHMOD_ADD;
				state = STATE_2ND_HALF;
				break;
			case '-':
				op = CHMOD_SUB;
				state = STATE_2ND_HALF;
				break;
			case '=':
				op = CHMOD_EQ;
				state = STATE_2ND_HALF;
				break;
			default:
				state = STATE_ERROR;
				break;
			}
		} else {
			switch (*modestr) {
			case 'r':
				what |= 4;
				break;
			case 'w':
				what |= 2;
				break;
			case 'X':
			  	flags |= FLAG_X_KEEP;
				/* FALL THROUGH */
			case 'x':
				what |= 1;
				break;
			case 's':
				if (topbits)
					topoct |= topbits;
				else
					topoct = 04000;
				break;
			case 't':
				topoct |= 01000;
				break;
			default:
				state = STATE_ERROR;
				break;
			}
		}
		modestr++;
	}

	if (state == STATE_ERROR) {
		free_chmod_mode(first_mode);
		return NULL;
	}

	if (append_to) {
		for (prev_mode = append_to; prev_mode->next; )
			prev_mode = prev_mode->next;
		prev_mode->next = first_mode;
		return append_to;
	}

	return first_mode;
}


/* Takes an existing file permission and a list of AND/OR changes, and
 * create a new permissions. */
int tweak_mode(int mode, struct chmod_mode_struct *chmod_modes)
{
	int IsX = mode & 0111;
	int NonPerm = mode & ~07777;

	for ( ; chmod_modes; chmod_modes = chmod_modes->next) {
		if ((chmod_modes->flags & FLAG_DIRS_ONLY) && !S_ISDIR(NonPerm))
			continue;
		if ((chmod_modes->flags & FLAG_FILES_ONLY) && S_ISDIR(NonPerm))
			continue;
		mode &= chmod_modes->ModeAND;
		if ((chmod_modes->flags & FLAG_X_KEEP) && !IsX && !S_ISDIR(NonPerm))
			mode |= chmod_modes->ModeOR & ~0111;
		else
			mode |= chmod_modes->ModeOR;
	}

	return mode | NonPerm;
}

/* Free the linked list created by parse_chmod. */
int free_chmod_mode(struct chmod_mode_struct *chmod_modes)
{
	struct chmod_mode_struct *next;

	while (chmod_modes) {
		next = chmod_modes->next;
		free(chmod_modes);
		chmod_modes = next;
	}
	return 0;
}