/**
 *	(C) Copyright 2006-2008, Magnus Gal�en
 *
 *	dstack.h: Stack-container.
 */

#ifndef _DSTACK_H_
#define _DSTACK_H_

#include <stdarg.h>

#include "dcontainer.h"


container* stack_container( container *C );

int stack_size( container *C );
void stack_pop( container *C );
value stack_peak( container *C );
void stack_push( container *C, ... );

inline value stack_get( container *C, int id );


#endif	// _DSTACK_H_
