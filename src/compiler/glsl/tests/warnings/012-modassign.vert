#version 130

void main()
{
  int defined = 2;
  int undefined;
  int myInt;

  myInt %= undefined;
  myInt %= defined;

  gl_Position = vec4(1.0);
}

