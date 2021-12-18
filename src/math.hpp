#pragma once

inline double squared(const double &num) {
  return num * num;
}

inline double squared_dist(const double &num, const double &num2) {
  return (num - num2) * (num - num2);
}

inline double get_distance_approx(double x, double y, double x2, double y2) {
  static const double sqrtOf2 = sqrt(2);
  const auto xAbs = abs(x - x2);
  const auto yAbs = abs(y - y2);
  return (1. + 1. / (4. - 2. * sqrtOf2)) / 2. * std::min((1. / sqrtOf2) * (xAbs + yAbs), std::max(xAbs, yAbs));
}

inline double get_distance(double x, double y, double x2, double y2) {
  return sqrt(squared_dist(x, x2) + squared_dist(y, y2));
}