#ifndef _CANSAT_LAUNCHER_REMOTE_H_
#define _CANSAT_LAUNCHER_REMOTE_H_



void warningHandler(Error::Module module=Error::Module::CUSTOM, int userModule=0, Error::Code code=0);
void criticalHandler(Error::Module module=Error::Module::CUSTOM, int userModule=0, Error::Code code=0);

#endif