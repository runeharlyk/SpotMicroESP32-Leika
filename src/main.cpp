#include <spot.h>

Spot spot;

void setup(){
  spot.boot();
}

void loop(){
  spot.handle();
}
