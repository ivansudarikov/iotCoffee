/*
 * IoTCoffeeMachine.h
 *
 *  Created on: 1 мая 2017 г.
 *      Author: isudarik
 */

#ifndef IOTCOFFEEMACHINE_H_
#define IOTCOFFEEMACHINE_H_

#include "IoTComponent.h"
#include "Arduino.h"

class IoTCoffeeMachine: public IoTComponent {
public:
	IoTCoffeeMachine(IoTESP8266* esp);
	virtual ~IoTCoffeeMachine();
};

#endif /* IOTCOFFEEMACHINE_H_ */
