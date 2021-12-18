#include "snowflake.h"

#include <random>

double tt = 0;
std::mt19937 mt;

snowflake::snowflake() {
  x = rand() * 640;
  y = rand() * 480;
  size = expf(rand(), 1000);

  radiussize = 2. + (4. * size);
  static_x = x;
  opacity = rand();
}

void snowflake::update() {
  double e = 0.04;
  y += 0.5 + (e * 200 * (size));
  x = static_x + (sin(tt * 10) * 200);
  while (y >= 480) {
    y -= 480;
  }
  while (x >= 640) {
    x -= 640;
  }
  while (x < 0) {
    x += 640;
  }
}

double snowflake::expf(double v, double factor) {
  auto max = factor;
  auto maxexp = log(max + 1.0) / log(2.0);
  auto linear = v;
  auto expf = ((pow(2.0, (linear)*maxexp)) - 1.0) / max;
  return expf;
}

double snowflake::rand() {
  return (mt() / (double)mt.max());
}