#ifndef __GETOPT_H__
#define __GETOPT_H__
#ifdef sgi
#pragma once
#endif
#ifdef __cplusplus
extern "C" {
#endif

/*
 * Declarations for getopt(3C).
 *
 * Copyright 1990, Silicon Graphics, Inc. 
 * All Rights Reserved.
 *
 * This is UNPUBLISHED PROPRIETARY SOURCE CODE of Silicon Graphics, Inc.;
 * the contents of this file may not be disclosed to third parties, copied or 
 * duplicated in any form, in whole or in part, without the prior written 
 * permission of Silicon Graphics, Inc.
 *
 * RESTRICTED RIGHTS LEGEND:
 * Use, duplication or disclosure by the Government is subject to restrictions 
 * as set forth in subdivision (c)(1)(ii) of the Rights in Technical Data
 * and Computer Software clause at DFARS 252.227-7013, and/or in similar or 
 * successor clauses in the FAR, DOD or NASA FAR Supplement. Unpublished - 
 * rights reserved under the Copyright Laws of the United States.
 */
#define	GETOPTDONE	(-1)

/*
 * This value is returned when an option argument is found that is not
 * in the option list
 */
#define	GETOPTHUH	'?'

extern int	getopt ARGS((int, char **, char *));

extern char	*optarg;
extern int	opterr;
extern int	optind;
extern int	optopt;

#ifdef __cplusplus
}
#endif

#endif /* !__GETOPT_H__ */
