/*
 * (c) copyright 1987 by the Vrije Universiteit, Amsterdam, The Netherlands.
 * See the copyright notice in the ACK home directory, in the file "Copyright".
 */
#define RCSID1 "$Id$"

/*
 * Mostek 6500 dependent C declarations.
 */

#define fits_zeropage(x)	(lowb(x) == (int)(x))

void branch(register int opc, expr_t exp);
void code(expr_t exp, register int opc1, register int opc2);
