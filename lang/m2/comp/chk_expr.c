/*
 * (c) copyright 1987 by the Vrije Universiteit, Amsterdam, The Netherlands.
 * See the copyright notice in the ACK home directory, in the file "Copyright".
 *
 * Author: Ceriel J.H. Jacobs
 */

/* E X P R E S S I O N   C H E C K I N G */

/* $Id$ */

/*	Check expressions, and try to evaluate them as far as possible.
*/

#include  	<stdlib.h>
#include  	<string.h>
#include	"parameters.h"
#include	"debug.h"

#include	<em_arith.h>
#include	<em_label.h>
#include	<assert.h>
#include	<alloc.h>
#include	<flt_arith.h>
#include	<system.h>

#include	"Lpars.h"
#include	"idf.h"
#include	"LLlex.h"
#include	"type.h"
#include	"def.h"
#include	"node.h"
#include	"scope.h"
#include	"error.h"
#include	"standards.h"
#include	"chk_expr.h"
#include    "cstoper.h"
#include    "typequiv.h"
#include	"misc.h"
#include	"lookup.h"
#include	"print.h"
#include	"warning.h"
#include	"main.h"

extern char *symbol2str();

/* Forward file declarations */
static int ChkStandard(struct node **);
static int ChkCast(struct node **);




static void df_error(
	struct node		*nd,		/* node on which error occurred */
	char		*mess,		/* error message */
	register struct def	*edf)		/* do we have a name? */
{
	if (edf) {
		if (edf->df_kind != D_ERROR)  {
			node_error(nd,"\"%s\": %s", edf->df_idf->id_text, mess);
		}
	}
	else node_error(nd, mess);
}

void MkCoercion(struct node **pnd, register struct type *tp)
{
	/*	Make a coercion from the node indicated by *pnd to the
		type indicated by tp. If the node indicated by *pnd
		is constant, try to do the coercion compile-time.
		Coercions are inserted in the tree when
		- the expression is not constant or
		- we are in the second pass and the coercion might cause
		  an error
	*/
	register struct node	*nd = *pnd;
	register struct type *nd_tp = nd->nd_type;
	extern int	pass_1;
	char		*wmess = 0;
	arith		op;

	if (nd_tp == tp || nd_tp->tp_fund == T_STRING /* Why ??? */) return;
	nd_tp = BaseType(nd_tp);
	if (nd->nd_class == Value && nd->nd_type != error_type && tp != error_type) {
		if (nd_tp->tp_fund == T_REAL) {
			switch(tp->tp_fund) {
			case T_REAL:
				nd->nd_type = tp;
				return;
			case T_CARDINAL:
				op = flt_flt2arith(&nd->nd_RVAL, 1);
				break;
			case T_INTEGER:
				op = flt_flt2arith(&nd->nd_RVAL, 0);
				break;
			default:
				crash("MkCoercion");
				/*NOTREACHED*/
			}
			if (flt_status == FLT_OVFL) {
				wmess = "conversion";
			}
			if (!wmess || pass_1) {
				if (nd->nd_RSTR) free(nd->nd_RSTR);
				free_real(nd->nd_REAL);
				nd->nd_INT = op;
				nd->nd_symb = INTEGER;
			}
		}
		switch(tp->tp_fund) {
		case T_REAL: {
			struct real *p = new_real();
			switch(BaseType(nd_tp)->tp_fund) {
			case T_CARDINAL:
			case T_INTORCARD:
				flt_arith2flt(nd->nd_INT, &p->r_val, 1);
				break;
			case T_INTEGER:
				flt_arith2flt(nd->nd_INT, &p->r_val, 0);
				break;
			default:
				crash("MkCoercion");
			}
			nd->nd_REAL = p;
			nd->nd_symb = REAL;
			}
			break;
		case T_SUBRANGE:
		case T_ENUMERATION:
		case T_CHAR:
			if (! in_range(nd->nd_INT, tp)) {
				wmess = "range bound";
			}
			break;
		case T_INTORCARD:
		case T_CARDINAL:
		case T_POINTER:
			if ((nd_tp->tp_fund == T_INTEGER && nd->nd_INT < 0) ||
			    (nd->nd_INT & ~full_mask[(int)(tp->tp_size)])) {
				wmess = "conversion";
			}
			break;
		case T_INTEGER:
			if (! chk_bounds(nd->nd_INT,
					 max_int[(int)(tp->tp_size)],
					 nd_tp->tp_fund) ||
			    ! chk_bounds(min_int[(int)(tp->tp_size)],
					 nd->nd_INT,
					 T_INTEGER)) {
				wmess = "conversion";
			}
			break;
		}
		if (wmess) {
		   node_warning(nd, W_ORDINARY, "might cause %s error", wmess);
		}
		if (!wmess || pass_1) {
			nd->nd_type = tp;
			return;
		}
	}
	*pnd = nd;
	nd = getnode(Uoper);
	nd->nd_symb = COERCION;
	nd->nd_type = tp;
	nd->nd_LEFT = NULLNODE;
	nd->nd_RIGHT = *pnd;
	nd->nd_lineno = (*pnd)->nd_lineno;
	*pnd = nd;
}

int ChkVariable(register struct node **expp, int flags)
{
	/*	Check that "expp" indicates an item that can be
		assigned to.
	*/
	register struct node *exp;

	if (! ChkDesig(expp, flags)) return 0;

	exp = *expp;
	if (exp->nd_class == Def &&
	    ! (exp->nd_def->df_kind & (D_FIELD|D_VARIABLE))) {
		df_error(exp, "variable expected", exp->nd_def);
		return 0;
	}
	return 1;
}

static int ChkArrow(struct node **expp, int flags)
{
	/*	Check an application of the '^' operator.
		The operand must be a variable of a pointer type.
	*/
	register struct type *tp;
	register struct node *exp = *expp;

	assert(exp->nd_class == Arrow);
	assert(exp->nd_symb == '^');

	exp->nd_type = error_type;

	if (! ChkVariable(&(exp->nd_RIGHT), D_USED)) return 0;

	tp = exp->nd_RIGHT->nd_type;

	if (tp->tp_fund != T_POINTER) {
		node_error(exp, "\"^\": illegal operand type");
		return 0;
	}

	if ((tp = RemoveEqual(PointedtoType(tp))) == 0) tp = error_type;
	exp->nd_type = tp;
	return 1;
}

static int ChkArr(struct node **expp, int flags)
{
	/*	Check an array selection.
		The left hand side must be a variable of an array type,
		and the right hand side must be an expression that is
		assignment compatible with the array-index.
	*/

	register struct type *tpl;
	register struct node *exp = *expp;

	assert(exp->nd_class == Arrsel);
	assert(exp->nd_symb == '[' || exp->nd_symb == ',');

	exp->nd_type = error_type;

	if (! (ChkVariable(&(exp->nd_LEFT), flags) &
	       ChkExpression(&(exp->nd_RIGHT)))) {
		/* Bitwise and, because we want them both evaluated.
		*/
		return 0;
	}

	tpl = exp->nd_LEFT->nd_type;

	if (tpl->tp_fund != T_ARRAY) {
		node_error(exp, "not indexing an ARRAY type");
		return 0;
	}
	exp->nd_type = RemoveEqual(tpl->arr_elem);

	/* Type of the index must be assignment compatible with
	   the index type of the array (Def 8.1).
	   However, the index type of a conformant array is not specified.
	   In our implementation it is CARDINAL.
	*/
	return ChkAssCompat(&(exp->nd_RIGHT),
			    BaseType(IndexType(tpl)),
			    "index type");
}

/*ARGSUSED*/
static int ChkValue(struct node **expp, int flags)
{
#ifdef DEBUG
	switch((*expp)->nd_symb) {
	case REAL:
	case STRING:
	case INTEGER:
		break;

	default:
		crash("(ChkValue)");
	}
#endif
	return 1;
}

static int ChkSelOrName(struct node **expp, int flags)
{
	/*	Check either an ID or a construction of the form
		ID.ID [ .ID ]*
	*/
	register struct def *df;
	register struct node *exp = *expp;

	exp->nd_type = error_type;

	if (exp->nd_class == Name) {
		df = lookfor(exp, CurrVis, 1, flags);
		exp = getnode(Def);
		exp->nd_def = df;
		exp->nd_lineno = (*expp)->nd_lineno;
		exp->nd_type = RemoveEqual(df->df_type);
		FreeNode(*expp);
		*expp = exp;
	}
	else if (exp->nd_class == Select) {
		/*	A selection from a record or a module.
			Modules also have a record type.
		*/
		register struct node *left;

		assert(exp->nd_symb == '.');

		if (! ChkDesig(&(exp->nd_NEXT), flags)) return 0;

		left = exp->nd_NEXT;
		if (left->nd_class==Def &&
		    (left->nd_type->tp_fund != T_RECORD ||
		    !(left->nd_def->df_kind & (D_MODULE|D_VARIABLE|D_FIELD))
		    )
		   ) {
			df_error(left, "illegal selection", left->nd_def);
			return 0;
		}
		if (left->nd_type->tp_fund != T_RECORD) {
			node_error(left, "illegal selection");
			return 0;
		}

		if (!(df = lookup(exp->nd_IDF, left->nd_type->rec_scope, D_IMPORTED, flags))) {
			id_not_declared(exp);
			return 0;
		}
		exp = getnode(Def);
		exp->nd_def = df;
		exp->nd_type = RemoveEqual(df->df_type);
		exp->nd_lineno = (*expp)->nd_lineno;
		free_node(*expp);
		*expp = exp;
		if (!(df->df_flags & (D_EXPORTED|D_QEXPORTED))) {
			/* Fields of a record are always D_QEXPORTED,
			   so ...
			*/
			df_error(exp, "not exported from qualifying module", df);
		}

		if (!(left->nd_class == Def &&
		      left->nd_def->df_kind == D_MODULE)) {
			exp->nd_NEXT = left;
			return 1;
		}
		FreeNode(left);
	}

	assert(exp->nd_class == Def);

	return exp->nd_def->df_kind != D_ERROR;
}

static int ChkExSelOrName(struct node **expp, int flags)
{
	/*	Check either an ID or an ID.ID [.ID]* occurring in an
		expression.
	*/
	register struct def *df;
	register struct node *exp;

	if (! ChkSelOrName(expp, D_USED)) return 0;

	exp = *expp;

	df = exp->nd_def;

	if (df->df_kind & (D_ENUM | D_CONST)) {
		/* Replace an enum-literal or a CONST identifier by its value.
		*/
		exp = getnode(Value);
		exp->nd_type = df->df_type;
		if (df->df_kind == D_ENUM) {
			exp->nd_INT = df->enm_val;
			exp->nd_symb = INTEGER;
		}
		else  {
			assert(df->df_kind == D_CONST);
			exp->nd_token = df->con_const;
		}
		exp->nd_lineno = (*expp)->nd_lineno;
		if (df->df_type->tp_fund == T_SET) {
			exp->nd_class = Set;
			inc_refcount(exp->nd_set);
		}
		else if (df->df_type->tp_fund == T_PROCEDURE) {
			/* for procedure constants */
			exp->nd_class = Def;
		}
		if (df->df_type->tp_fund == T_REAL) {
			struct real *p = exp->nd_REAL;

			exp->nd_REAL = new_real();
			*(exp->nd_REAL) = *p;
			if (p->r_real) {
				p->r_real = Salloc(p->r_real,
					   (unsigned)(strlen(p->r_real)+1));
			}
		}
		FreeNode(*expp);
		*expp = exp;
	}

	if (!(df->df_kind & D_VALUE)) {
		df_error(exp, "value expected", df);
		return 0;
	}

	if (df->df_kind == D_PROCEDURE) {
		/* Check that this procedure is one that we may take the
		   address from.
		*/
		if (df->df_type == std_type || df->df_scope->sc_level > 0) {
			/* Address of standard or nested procedure
			   taken.
			*/
			node_error(exp,
			   "standard or local procedures may not be assigned");
			return 0;
		}
	}

	return 1;
}

static int ChkEl(register struct node **expp, struct type *tp)
{

	return ChkExpression(expp) && ChkCompat(expp, tp, "set element");
}

static int ChkElement(struct node **expp, struct type *tp, arith *set)
{
	/*	Check elements of a set. This routine may call itself
		recursively.
		Also try to compute the set!
	*/
	register struct node *expr = *expp;
	struct type *el_type = ElementType(tp);
	register unsigned int i;
	arith low, high;

	if (expr->nd_class == Link && expr->nd_symb == UPTO) {
		/* { ... , expr1 .. expr2,  ... }
		   First check expr1 and expr2, and try to compute them.
		*/
		if (! (ChkEl(&(expr->nd_LEFT), el_type) & 
		       ChkEl(&(expr->nd_RIGHT), el_type))) {
			return 0;
		}

		if (!(expr->nd_LEFT->nd_class == Value &&
		      expr->nd_RIGHT->nd_class == Value)) {
			return 1;
		}
		/* We have a constant range. Put all elements in the
		  set
		*/

		low = expr->nd_LEFT->nd_INT;
		high = expr->nd_RIGHT->nd_INT;
	}
	else {
		if (! ChkEl(expp, el_type)) return 0;
		expr = *expp;
		if (expr->nd_class != Value) {
			return 1;
		}
		low = high = expr->nd_INT;
	}
	if (! chk_bounds(low, high, BaseType(el_type)->tp_fund)) {
		node_error(expr, "lower bound exceeds upper bound in range");
		return 0;
	}

	if (! in_range(low, el_type) || ! in_range(high, el_type)) {
		node_error(expr, "set element out of range");
		return 0;
	}

	low -= tp->set_low;
	high -= tp->set_low;
	for (i=(unsigned)low; i<= (unsigned)high; i++) {
		set[i/wrd_bits] |= (1<<(i%wrd_bits));
	}
	FreeNode(expr);
	*expp = 0;
	return 1;
}

arith *MkSet(unsigned int size)
{
	register arith	*s, *t;

	s = t = (arith *) Malloc(size);
	s++;
	size /= sizeof(arith);
	while (size--) *t++ = 0;
	inc_refcount(s);
	return s;
}

void FreeSet(register arith *s)
{
	dec_refcount(s);
	if (refcount(s) <= 0) {
		assert(refcount(s) == 0);
		free((char *) (s-1));
	}
}

static int ChkSet(struct node **expp, int flags)
{
	/*	Check the legality of a SET aggregate, and try to evaluate it
		compile time. Unfortunately this is all rather complicated.
	*/
	register struct type *tp;
	register struct node *exp = *expp;
	register struct node *nd;
	register struct def *df;
	int retval = 1;
	int SetIsConstant = 1;

	assert(exp->nd_symb == SET);

	*expp = getnode(Set);
	(*expp)->nd_type = error_type;
	(*expp)->nd_lineno = exp->nd_lineno;

	/* First determine the type of the set
	*/
	if (exp->nd_LEFT) {
		/* A type was given. Check it out
		*/
		if (! ChkDesig(&(exp->nd_LEFT), D_USED)) return 0;
		nd = exp->nd_LEFT;
		assert(nd->nd_class == Def);
		df = nd->nd_def;

		if (!is_type(df) ||
	   	    (df->df_type->tp_fund != T_SET)) {
			df_error(nd, "not a SET type", df);
			return 0;
		}
		tp = df->df_type;
	}
	else	tp = bitset_type;
	(*expp)->nd_type = tp;

	nd = exp->nd_RIGHT;

	/* Now check the elements given, and try to compute a constant set.
	   First allocate room for the set.
	*/

	(*expp)->nd_set = MkSet(tp->set_sz);

	/* Now check the elements, one by one
	*/
	while (nd) {
		assert(nd->nd_class == Link && nd->nd_symb == ',');

		if (!ChkElement(&(nd->nd_LEFT), tp, (*expp)->nd_set)) {
			retval = 0;
		}
		if (nd->nd_LEFT) SetIsConstant = 0;
		nd = nd->nd_RIGHT;
	}

	if (! SetIsConstant) {
		(*expp)->nd_NEXT = exp->nd_RIGHT;
		exp->nd_RIGHT = 0;
	}
	FreeNode(exp);
	return retval;
}

static struct node *nextarg(struct node **argp, struct def *edf)
{
	register struct node *arg = (*argp)->nd_RIGHT;

	if (! arg) {
		df_error(*argp, "too few arguments supplied", edf);
		return 0;
	}

	*argp = arg;
	return arg;
}

static struct node *getarg(struct node **argp, int bases, int designator, struct def *edf)
{
	/*	This routine is used to fetch the next argument from an
		argument list. The argument list is indicated by "argp".
		The parameter "bases" is a bitset indicating which types
		are allowed at this point, and "designator" is a flag
		indicating that the address from this argument is taken, so
		that it must be a designator and may not be a register
		variable.
	*/
	register struct node *arg = nextarg(argp, edf);
	register struct node *left;

	if (! arg ||
	    ! arg->nd_LEFT ||
	    ! (designator ? ChkVariable(&(arg->nd_LEFT), D_USED|D_DEFINED) : ChkExpression(&(arg->nd_LEFT)))) {
		return 0;
	}
	left = arg->nd_LEFT;

	if (designator && left->nd_class==Def) {
		left->nd_def->df_flags |= D_NOREG;
	}

	if (bases) {
		struct type *tp = BaseType(left->nd_type);

		if (! designator) MkCoercion(&(arg->nd_LEFT), tp);
		left = arg->nd_LEFT;
		if (!(tp->tp_fund & bases)) {
			df_error(left, "unexpected parameter type", edf);
			return 0;
		}
	}

	return left;
}

static struct node *getname(struct node **argp, int kinds, int bases, struct def *edf)
{
	/*	Get the next argument from argument list "argp".
		The argument must indicate a definition, and the
		definition kind must be one of "kinds".
	*/
	register struct node *arg = nextarg(argp, edf);
	register struct node *left;

	if (!arg || !arg->nd_LEFT || ! ChkDesig(&(arg->nd_LEFT), D_USED)) return 0;

	left = arg->nd_LEFT;
	if (left->nd_class != Def) {
		df_error(left, "identifier expected", edf);
		return 0;
	}

	if (!(left->nd_def->df_kind & kinds) ||
	    (bases && !(left->nd_type->tp_fund & bases))) {
		df_error(left, "unexpected parameter type", edf);
		return 0;
	}

	return left;
}

static int ChkProcCall(register struct node *exp)
{
	/*	Check a procedure call
	*/
	register struct node *left;
	struct node *argp;
	struct def *edf = 0;
	register struct paramlist *param;
	int retval = 1;
	int cnt = 0;

	left = exp->nd_LEFT;
	if (left->nd_class == Def) {
		edf = left->nd_def;
	}
	if (left->nd_type == error_type) {
		/* Just check parameters as if they were value parameters
		*/
		argp = exp;
		while (argp->nd_RIGHT) {
			if (getarg(&argp, 0, 0, edf)) { }
		}
		return 0;
	}

	exp->nd_type = RemoveEqual(ResultType(left->nd_type));

	/* Check parameter list
	*/
	argp = exp;
	for (param = ParamList(left->nd_type); param; param = param->par_next) {
		if (!(left = getarg(&argp, 0, IsVarParam(param), edf))) {
			retval = 0;
			cnt++;
			continue;
		}
		cnt++;
		if (left->nd_symb == STRING) {
			TryToString(left, TypeOfParam(param));
		}
		if (! TstParCompat(cnt,
				   RemoveEqual(TypeOfParam(param)),
				   IsVarParam(param),
				   &(argp->nd_LEFT),
				   edf)) {
			retval = 0;
		}
	}

	exp = argp;
	if (exp->nd_RIGHT) {
		df_error(exp->nd_RIGHT,"too many parameters supplied",edf);
		while (argp->nd_RIGHT) {
			if (getarg(&argp, 0, 0, edf)) { }
		}
		return 0;
	}

	return retval;
}

static int ChkFunCall(register struct node **expp, int flags)
{
	/*	Check a call that must have a result
	*/

	if (ChkCall(expp)) {
		if ((*expp)->nd_type != 0) return 1;
		node_error(*expp, "function call expected");
	}
	(*expp)->nd_type = error_type;
	return 0;
}



int ChkCall(struct node **expp)
{
	/*	Check something that looks like a procedure or function call.
		Of course this does not have to be a call at all,
		it may also be a cast or a standard procedure call.
	*/

	/* First, get the name of the function or procedure
	*/
	if (ChkDesig(&((*expp)->nd_LEFT), D_USED)) {
		register struct node *left = (*expp)->nd_LEFT;
		
		if (IsCast(left)) {
			/* It was a type cast.
			*/
			return ChkCast(expp);
		}

		if (IsProc(left) || left->nd_type == error_type) {
			/* A procedure call.
		   	   It may also be a call to a standard procedure
			*/
			if (left->nd_type == std_type) {
				/* A standard procedure
				*/
				return ChkStandard(expp);
			}
			/* Here, we have found a real procedure call. 
			   The left hand side may also represent a procedure
			   variable.
			*/
		}
		else {
			node_error(left, "procedure, type, or function expected");
			left->nd_type = error_type;
		}
	}
	return ChkProcCall(*expp);
}

static struct type *ResultOfOperation(int operator, struct type *tp)
{
	/*	Return the result type of the binary operation "operator",
		with operand type "tp".
	*/

	switch(operator) {
	case '=':
	case '#':
	case GREATEREQUAL:
	case LESSEQUAL:
	case '<':
	case '>':
	case IN:
		return bool_type;
	}

	return tp;
}

#define Boolean(operator) (operator == OR || operator == AND)

static int AllowedTypes(int operator)
{
	/*	Return a bit mask indicating the allowed operand types
		for binary operator "operator".
	*/

	switch(operator) {
	case '+':
	case '-':
	case '*':
		return T_NUMERIC|T_SET;
	case '/':
		return T_REAL|T_SET;
	case DIV:
	case MOD:
		return T_INTORCARD;
	case OR:
	case AND:
		return T_ENUMERATION;
	case '=':
	case '#':
		return T_POINTER|T_HIDDEN|T_SET|T_NUMERIC|T_ENUMERATION|T_CHAR;
	case GREATEREQUAL:
	case LESSEQUAL:
		return T_SET|T_NUMERIC|T_CHAR|T_ENUMERATION;
	case '<':
	case '>':
		return T_NUMERIC|T_CHAR|T_ENUMERATION;
	default:
		crash("(AllowedTypes)");
	}
	/*NOTREACHED*/
}

static int ChkAddressOper(
	register struct type *tpl,
	register struct type *tpr,
	register struct node *expp)
{
	/*	Check that either "tpl" or "tpr" are both of type
		address_type, or that one of them is, but the other is
		of a cardinal type.
		Also insert proper coercions, making sure that the EM pointer
		arithmetic instructions can be generated whenever possible
	*/

	if (tpr == address_type && expp->nd_symb == '+') {
		/* use the fact that '+' is a commutative operator */
		struct type *tmptype = tpr;
		struct node *tmpnode = expp->nd_RIGHT;

		tpr = tpl;
		expp->nd_RIGHT = expp->nd_LEFT;
		tpl = tmptype;
		expp->nd_LEFT = tmpnode;
	}
	
	if (tpl == address_type) {
		expp->nd_type = address_type;
		if (tpr == address_type) {
			return 1;
		}
		if (tpr->tp_fund & T_CARDINAL) {
			MkCoercion(&(expp->nd_RIGHT),
				   expp->nd_symb=='+' || expp->nd_symb=='-' ?
					tpr :
				  	address_type);
			return 1;
		}
		return 0;
	}

	if (tpr == address_type && tpl->tp_fund & T_CARDINAL) {
		expp->nd_type = address_type;
		MkCoercion(&(expp->nd_LEFT), address_type);
		return 1;
	}

	return 0;
}

static int ChkBinOper(struct node **expp, int flags)
{
	/*	Check a binary operation.
	*/
	register struct node *exp = *expp;
	register struct type *tpl, *tpr;
	struct type *result_type;
	int allowed;
	int retval;
	char *symb;

	/* First, check BOTH operands */

	retval = ChkExpression(&(exp->nd_LEFT));
	retval &= ChkExpression(&(exp->nd_RIGHT));

	tpl = BaseType(exp->nd_LEFT->nd_type);
	tpr = BaseType(exp->nd_RIGHT->nd_type);

	if (intorcard(tpl, tpr) != 0) {
		if (tpl->tp_fund == T_INTORCARD) {
			 exp->nd_LEFT->nd_type = tpl = tpr;
		}
		if (tpr->tp_fund == T_INTORCARD) {
			exp->nd_RIGHT->nd_type = tpr = tpl;
		}
	}

	exp->nd_type = result_type = ResultOfOperation(exp->nd_symb, tpr);

	/* Check that the application of the operator is allowed on the type
	   of the operands.
	   There are three tricky parts:
	   - Boolean operators are only allowed on boolean operands, but
	     the "allowed-mask" of "AllowedTypes" can only indicate
	     an enumeration type.
	   - All operations that are allowed on CARDINALS are also allowed
	     on ADDRESS.
	   - The IN-operator has as right-hand-size operand a set.
	*/
	if (exp->nd_symb == IN) {
		if (tpr->tp_fund != T_SET) {
			node_error(exp, "\"IN\": right operand must be a set");
			return 0;
		}
		if (!TstAssCompat(ElementType(tpr), tpl)) {
			/* Assignment compatible ???
			   I don't know! Should we be allowed to check
			   if a INTEGER is a member of a BITSET???
			*/
			node_error(exp->nd_LEFT, "type incompatibility in IN");
			return 0;
		}
		MkCoercion(&(exp->nd_LEFT), word_type);
		if (exp->nd_LEFT->nd_class == Value &&
		    exp->nd_RIGHT->nd_class == Set &&
		    ! exp->nd_RIGHT->nd_NEXT) {
			cstset(expp);
		}
		return retval;
	}

	if (!retval) return 0;

	allowed = AllowedTypes(exp->nd_symb);

	symb = symbol2str(exp->nd_symb);
	if (!(tpr->tp_fund & allowed) || !(tpl->tp_fund & allowed)) {
	    	if (!((T_CARDINAL & allowed) &&
	             ChkAddressOper(tpl, tpr, exp))) {
			node_error(exp, "\"%s\": illegal operand type(s)", symb);
			return 0;
		}
		if (result_type == bool_type) exp->nd_type = bool_type;
	}
	else {
		if (Boolean(exp->nd_symb) && tpl != bool_type) {
			node_error(exp, "\"%s\": illegal operand type(s)", symb);
			return 0;
		}

		/* Operands must be compatible (distilled from Def 8.2)
		*/
		if (!TstCompat(tpr, tpl)) {
			extern char *incompat();
			node_error(exp, "\"%s\": %s in operands", symb, incompat(tpl, tpr));
			return 0;
		}

		MkCoercion(&(exp->nd_LEFT), tpl);
		MkCoercion(&(exp->nd_RIGHT), tpr);
	}

	if (tpl->tp_fund == T_SET) {
	    	if (exp->nd_LEFT->nd_class == Set &&
		    ! exp->nd_LEFT->nd_NEXT &&
		    exp->nd_RIGHT->nd_class == Set &&
			! exp->nd_RIGHT->nd_NEXT) {
			cstset(expp);
		}
	}
	else if ( exp->nd_LEFT->nd_class == Value &&
		  exp->nd_RIGHT->nd_class == Value) {
		if (tpl->tp_fund == T_INTEGER) {
			cstibin(expp);
		}
		else if (tpl->tp_fund == T_REAL) {
			cstfbin(expp);
		}
		else	cstubin(expp);
	}

	return 1;
}

static int ChkUnOper(struct node **expp, int flags)
{
	/*	Check an unary operation.
	*/
	register struct node *exp = *expp;
	register struct node *right = exp->nd_RIGHT;
	register struct type *tpr;

	if (exp->nd_symb == COERCION) return 1;
	if (exp->nd_symb == '(') {
		*expp = right;
		free_node(exp);
		return ChkExpression(expp);
	}
	exp->nd_type = error_type;
	if (! ChkExpression(&(exp->nd_RIGHT))) return 0;
	exp->nd_type = tpr = BaseType(exp->nd_RIGHT->nd_type);
	MkCoercion(&(exp->nd_RIGHT), tpr);
	right = exp->nd_RIGHT;

	if (tpr == address_type) tpr = card_type;

	switch(exp->nd_symb) {
	case '+':
		if (!(tpr->tp_fund & T_NUMERIC)) break;
		*expp = right;
		free_node(exp);
		return 1;

	case '-':
		if (tpr->tp_fund == T_INTORCARD || tpr->tp_fund == T_INTEGER) {
			if (tpr == intorcard_type) {
				exp->nd_type = int_type;
			}
			else if (tpr == longintorcard_type) {
				exp->nd_type = longint_type;
			}
			if (right->nd_class == Value) {
				cstunary(expp);
			}
			return 1;
		}
		else if (tpr->tp_fund == T_REAL) {
			if (right->nd_class == Value) {
				*expp = right;
				flt_umin(&(right->nd_RVAL));
				if (right->nd_RSTR) {
					free(right->nd_RSTR);
					right->nd_RSTR = 0;
				}
				free_node(exp);
			}
			return 1;
		}
		break;

	case NOT:
	case '~':
		if (tpr == bool_type) {
			if (right->nd_class == Value) {
				cstunary(expp);
			}
			return 1;
		}
		break;

	default:
		crash("ChkUnOper");
	}
	node_error(exp, "\"%s\": illegal operand type", symbol2str(exp->nd_symb));
	return 0;
}

static struct node *getvariable(struct node **argp, struct def *edf, int flags)
{
	/*	Get the next argument from argument list "argp".
		It must obey the rules of "ChkVariable".
	*/
	register struct node *arg = nextarg(argp, edf);

	if (! arg ||
	    ! arg->nd_LEFT ||
	    ! ChkVariable(&(arg->nd_LEFT), flags)) return 0;

	return arg->nd_LEFT;
}

static int ChkStandard(struct node **expp)
{
	/*	Check a call of a standard procedure or function
	*/
	register struct node *exp = *expp;
	struct node *arglink = exp;
	register struct node *arg;
	register struct def *edf = exp->nd_LEFT->nd_def;
	int free_it = 0;
	int isconstant = 0;

	assert(exp->nd_LEFT->nd_class == Def);

	exp->nd_type = error_type;
	switch(edf->df_value.df_stdname) {
	case S_ABS:
		if (!(arg = getarg(&arglink, T_NUMERIC, 0, edf))) return 0;
		exp->nd_type = BaseType(arg->nd_type);
		MkCoercion(&(arglink->nd_LEFT), exp->nd_type);
		arg = arglink->nd_LEFT;
		if (! (exp->nd_type->tp_fund & (T_INTEGER|T_REAL))) {
			free_it = 1;
		}
		if (arg->nd_class == Value) {
			switch(exp->nd_type->tp_fund) {
			case T_REAL:
				arg->nd_RVAL.flt_sign = 0;
				free_it = 1;
				break;
			case T_INTEGER:
				isconstant = 1;
				break;
			}
		}
		break;

	case S_CAP:
		exp->nd_type = char_type;
		if (!(arg = getarg(&arglink, T_CHAR, 0, edf))) return 0;
		if (arg->nd_class == Value) isconstant = 1;
		break;

	case S_FLOATD:
	case S_FLOAT:
		if (! getarg(&arglink, T_INTORCARD, 0, edf)) return 0;
		arg = arglink;
		if (edf->df_value.df_stdname == S_FLOAT) {
			MkCoercion(&(arg->nd_LEFT), card_type);
		}
		MkCoercion(&(arg->nd_LEFT),
			   edf->df_value.df_stdname == S_FLOATD ?
				longreal_type :
				real_type);
		free_it = 1;
		break;

	case S_SHORT:
	case S_LONG: {
		struct type *tp;
		struct type *s1, *s2, *s3, *d1, *d2, *d3;

		if (!(arg = getarg(&arglink, 0, 0, edf))) {
			return 0;
		}
		tp = BaseType(arg->nd_type);

		if (edf->df_value.df_stdname == S_SHORT) {
			s1 = longint_type;
			d1 = int_type;
			s2 = longreal_type;
			d2 = real_type;
			s3 = longcard_type;
			d3 = card_type;
		}
		else {
			d1 = longint_type;
			s1 = int_type;
			d2 = longreal_type;
			s2 = real_type;
			d3 = longcard_type;
			s3 = card_type;
		}

		if (tp == s1) {
			MkCoercion(&(arglink->nd_LEFT), d1);
		}
		else if (tp == s2) {
			MkCoercion(&(arglink->nd_LEFT), d2);
		}
		else if (options['l'] && tp == s3) {
			MkCoercion(&(arglink->nd_LEFT), d3);
		}
		else {
			df_error(arg, "unexpected parameter type", edf);
			break;
		}
		free_it = 1;
		break;
		}

	case S_HIGH:
		if (!(arg = getarg(&arglink, T_ARRAY|T_STRING|T_CHAR, 0, edf))) {
			return 0;
		}
		if (arg->nd_type->tp_fund == T_ARRAY) {
			exp->nd_type = IndexType(arg->nd_type);
			if (! IsConformantArray(arg->nd_type)) {
				arg->nd_type = exp->nd_type;
				isconstant = 1;
			}
			break;
		}
		if (arg->nd_symb != STRING) {
			df_error(arg,"array parameter expected", edf);
			return 0;
		}
		exp = getnode(Value);
		exp->nd_type = card_type;
		/* Notice that we could disallow HIGH("") here by checking
		   that arg->nd_type->tp_fund != T_CHAR || arg->nd_INT != 0.
		   ??? For the time being, we don't. !!!
		   Maybe the empty string should not be allowed at all.
		*/
		exp->nd_INT = arg->nd_type->tp_fund == T_CHAR ? 0 :
					arg->nd_SLE - 1;
		exp->nd_symb = INTEGER;
		exp->nd_lineno = (*expp)->nd_lineno;
		(*expp)->nd_RIGHT = 0;
		FreeNode(*expp);
		*expp = exp;
		break;

	case S_MAX:
	case S_MIN:
		if (!(arg = getname(&arglink, D_ISTYPE, T_DISCRETE, edf))) {
			return 0;
		}
		exp->nd_type = arg->nd_type;
		isconstant = 1;
		break;

	case S_ODD:
		if (! (arg = getarg(&arglink, T_INTORCARD, 0, edf))) return 0;
		MkCoercion(&(arglink->nd_LEFT), BaseType(arg->nd_type));
		exp->nd_type = bool_type;
		if (arglink->nd_LEFT->nd_class == Value) isconstant = 1;
		break;

	case S_ORD:
		if (! (arg = getarg(&arglink, T_NOSUB, 0, edf))) return 0;
		exp->nd_type = card_type;
		if (arg->nd_class == Value) {
			arg->nd_type = card_type;
			free_it = 1;
		}
		break;

#ifndef STRICT_3RD_ED
	case S_NEW:
	case S_DISPOSE:
		{
			static int warning_given = 0;

			if (!warning_given) {
				warning_given = 1;
				if (! options['3'])
	node_warning(exp, W_OLDFASHIONED, "NEW and DISPOSE are obsolete");
				else
	node_error(exp, "NEW and DISPOSE are obsolete");
			}
		}
		exp->nd_type = 0;
		arg = getvariable(&arglink, edf, D_USED|D_DEFINED);
		if (! arg) return 0;
		if (! (arg->nd_type->tp_fund == T_POINTER)) {
			df_error(arg, "pointer variable expected", edf);
			return 0;
		}
		/* Now, make it look like a call to ALLOCATE or DEALLOCATE */
		arglink->nd_RIGHT = arg = getnode(Link);
		arg->nd_lineno = exp->nd_lineno;
		arg->nd_symb = ',';
		arg->nd_LEFT = getnode(Value);
		arg = arg->nd_LEFT;
		arg->nd_INT = PointedtoType(arglink->nd_LEFT->nd_type)->tp_size;
		arg->nd_symb = INTEGER;
		arg->nd_lineno = exp->nd_lineno;
		arg->nd_type = card_type;
		/* Ignore other arguments to NEW and/or DISPOSE ??? */

		FreeNode(exp->nd_LEFT);
		exp->nd_LEFT = arg = getnode(Name);
		arg->nd_symb = IDENT;
		arg->nd_lineno = exp->nd_lineno;
		arg->nd_IDF = str2idf(edf->df_value.df_stdname==S_NEW ?
					"ALLOCATE" : "DEALLOCATE", 0);
		return ChkCall(expp);
#endif

	case S_TSIZE:	/* ??? */
	case S_SIZE:
		exp->nd_type = intorcard_type;
		if (!(arg = getname(&arglink,D_FIELD|D_VARIABLE|D_ISTYPE,0,edf))) {
			return 0;
		}
		if (! IsConformantArray(arg->nd_type)) isconstant = 1;
#ifndef NOSTRICT
		else node_warning(exp,
				  W_STRICT,
				  "%s on conformant array",
				  edf->df_idf->id_text);
#endif
#ifndef STRICT_3RD_ED
		if (! options['3'] && edf->df_value.df_stdname == S_TSIZE) {
			if ( (arg = arglink->nd_RIGHT) ) {
				node_warning(arg,
					     W_OLDFASHIONED,
					     "TSIZE with multiple parameters, only first parameter used");
				FreeNode(arg);
				arglink->nd_RIGHT = 0;
			}
		}
#endif
		break;

	case S_TRUNCD:
	case S_TRUNC:
		if (! getarg(&arglink, T_REAL, 0, edf)) return 0;
		MkCoercion(&(arglink->nd_LEFT),
			   edf->df_value.df_stdname == S_TRUNCD ?
				options['l'] ? longcard_type : longint_type
					: card_type);
		free_it = 1;
		break;

	case S_VAL:
		if (!(arg = getname(&arglink, D_ISTYPE, T_NOSUB, edf))) {
			return 0;
		}
		exp->nd_type = arg->nd_def->df_type;
		exp->nd_RIGHT = arglink->nd_RIGHT;
		arglink->nd_RIGHT = 0;
		FreeNode(arglink);
		arglink = exp;
		/* fall through */
	case S_CHR:
		if (! getarg(&arglink, T_CARDINAL, 0, edf)) return 0;
		if (edf->df_value.df_stdname == S_CHR) {
			exp->nd_type = char_type;
		}
		if (exp->nd_type != int_type) {
			MkCoercion(&(arglink->nd_LEFT), exp->nd_type);
			free_it = 1;
		}
		break;

	case S_ADR:
		exp->nd_type = address_type;
		if (! getarg(&arglink, 0, 1, edf)) return 0;
		break;

	case S_DEC:
	case S_INC:
		exp->nd_type = 0;
		if (! (arg = getvariable(&arglink, edf, D_USED|D_DEFINED))) return 0;
		if (! (arg->nd_type->tp_fund & T_DISCRETE)) {
			df_error(arg,"illegal parameter type", edf);
			return 0;
		}
		if (arglink->nd_RIGHT) {
			if (! getarg(&arglink, T_INTORCARD, 0, edf)) return 0;
		}
		break;

	case S_HALT:
		exp->nd_type = 0;
		break;

	case S_EXCL:
	case S_INCL:
		{
		register struct type *tp;
		struct node *dummy;

		exp->nd_type = 0;
		if (!(arg = getvariable(&arglink, edf, D_USED|D_DEFINED))) return 0;
		tp = arg->nd_type;
		if (tp->tp_fund != T_SET) {
			df_error(arg, "SET parameter expected", edf);
			return 0;
		}
		if (!(dummy = getarg(&arglink, 0, 0, edf))) return 0;
		if (!ChkAssCompat(&dummy, ElementType(tp), "EXCL/INCL")) {
			/* What type of compatibility do we want here?
			   apparently assignment compatibility! ??? ???
			   But we don't want the coercion in the tree, because
			   we don't want a range check here. We want a SET
			   error.
			*/
			return 0;
		}
		MkCoercion(&(arglink->nd_LEFT), word_type);
		break;
		}

	default:
		crash("(ChkStandard)");
	}

	arg = arglink;

	if (arg->nd_RIGHT) {
		df_error(arg->nd_RIGHT, "too many parameters supplied", edf);
		return 0;
	}

	if (isconstant) {
		cstcall(expp, edf->df_value.df_stdname);
		return 1;
	}
	if (free_it) {
		*expp = arg->nd_LEFT;
		exp->nd_RIGHT = arg;
		arg->nd_LEFT = 0;
		FreeNode(exp);
	}

	return 1;
}

static int ChkCast(struct node **expp)
{
	/*	Check a cast and perform it if the argument is constant.
		If the sizes don't match, only complain if at least one of them
		has a size larger than the word size.
		If both sizes are equal to or smaller than the word size, there
		is no problem as such values take a word on the EM stack
		anyway.
	*/
	register struct node *exp = *expp;
	register struct node *arg = exp->nd_RIGHT;
	register struct type *lefttype = exp->nd_LEFT->nd_type;
	struct def		*df = exp->nd_LEFT->nd_def;

	if ((! arg) || arg->nd_RIGHT) {
		df_error(exp, "type cast must have 1 parameter", df);
		return 0;
	}

	if (! ChkExpression(&(arg->nd_LEFT))) return 0;

	MkCoercion(&(arg->nd_LEFT), BaseType(arg->nd_LEFT->nd_type));

	arg = arg->nd_LEFT;
	if (arg->nd_type->tp_size != lefttype->tp_size &&
	    (arg->nd_type->tp_size > word_size ||
	     lefttype->tp_size > word_size)) {
		df_error(exp, "unequal sizes in type cast", df);
		return 0;
	}

	if (IsConformantArray(arg->nd_type)) {
		df_error(exp,
		  "type transfer function on conformant array not supported",
		  df);
		return 0;
	}

	exp->nd_RIGHT->nd_LEFT = 0;
	FreeNode(exp);
	if (arg->nd_class == Value) {
		exp = arg;
		if (lefttype->tp_fund == T_SET) {
			/* User deserves what he gets here ... */
			exp = getnode(Set);
			exp->nd_set = MkSet((unsigned)(lefttype->set_sz));
			exp->nd_set[0] = arg->nd_INT;
			exp->nd_lineno = arg->nd_lineno;
			FreeNode(arg);
		}
	}
	else {
		exp = getnode(Uoper);
		exp->nd_symb = CAST;
		exp->nd_lineno = arg->nd_lineno;
		exp->nd_RIGHT = arg;
	}
	*expp = exp;
	exp->nd_type = lefttype;

	return 1;
}

void TryToString(register struct node *nd, struct type *tp)
{
	/*	Try a coercion from character constant to string.
	*/
	static char buf[8];

	assert(nd->nd_symb == STRING);

	if (tp->tp_fund == T_ARRAY && nd->nd_type == char_type) {
		buf[0] = nd->nd_INT;
		nd->nd_type = standard_type(T_STRING, 1, (arith) 2);
		nd->nd_SSTR = 
			(struct string *) Malloc(sizeof(struct string));
		nd->nd_STR = Salloc(buf, (unsigned) word_size);
		nd->nd_SLE = 1;
	}
}

static int no_desig(struct node **expp, int flags)
{
	node_error(*expp, "designator expected");
	return 0;
}

static int add_flags(struct node **expp, int flags)
{
	(*expp)->nd_def->df_flags |= flags;
	return 1;
}


int (*ExprChkTable[])(struct node **, int) = {
	ChkValue,
	ChkArr,
	ChkBinOper,
	ChkUnOper,
	ChkArrow,
	ChkFunCall,
	ChkExSelOrName,
	PNodeCrash,
	ChkSet,
	add_flags,
	PNodeCrash,
	ChkExSelOrName,
	PNodeCrash,
};

int (*DesigChkTable[])(struct node **, int) = {
	no_desig,
	ChkArr,
	no_desig,
	no_desig,
	ChkArrow,
	no_desig,
	ChkSelOrName,
	PNodeCrash,
	no_desig,
	add_flags,
	PNodeCrash,
	ChkSelOrName,
	PNodeCrash,
};
