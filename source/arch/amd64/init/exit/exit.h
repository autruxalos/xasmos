/******************************************************************************
 * XASMOS - System bootstrap (EXIT)
 * Path: source/arch/amd64/init/exit/exit.h
 ******************************************************************************/
#ifndef XASMOS_EXIT_H
#define XASMOS_EXIT_H

/* First userland code, handed off by the exokernel. Runs the bootstrap stages
 * in order, then gives control to the shell. Never returns. */
void exit_main_executor(void);

#endif /* XASMOS_EXIT_H */
