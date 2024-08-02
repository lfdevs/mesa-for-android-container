#version 130

void main()
{
  float foo;
  float undefinedThenDefined;

  foo = undefinedThenDefined;
  undefinedThenDefined = 2.0;
  foo = undefinedThenDefined;

  gl_Position = vec4(1.0);
}

