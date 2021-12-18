#pragma once

class snowflake {
private:
  int static_x = 0;

public:
  double x = 0;
  double y = 0;
  double size = 0;
  double opacity = 1.;
  double radiussize = 0;

  snowflake();
  void update();
  double expf(double v, double factor);

private:
  double rand();
};