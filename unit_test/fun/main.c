extern double f(double);
extern double ffmin(double, double(*fun)(double));

int main(int argc, char** argv)
{
  double x = 1.3;
  double z;
  z = ffmin(x, f);
  printf("z: %f\n", z);
  return 0;
}

