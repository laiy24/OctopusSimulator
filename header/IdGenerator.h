/*
 * File  :      MCoreSimProjectXml.h
 * Author:      Salah Hessien
 * Email :      salahga@mcmaster.ca
 *
 * Created On February 16, 2020
 */

#ifndef _IDGENERATOR_H
#define	_IDGENERATOR_H

#include <cstdint>

class IdGenerator {
private:
    static uint64_t reqId;

public:
    static uint64_t nextReqId() {
        return reqId++;
    }


};

#endif	/* _IDGENERATOR_H */

