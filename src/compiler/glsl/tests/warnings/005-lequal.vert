#version 130

void main()
{
   int undefined;
   int defined = 2;
   float fooFloat;

   if (undefined <= 0) {
      fooFloat = 10.0;
   }

   if (defined <= 0) {
      fooFloat = 10.0;
   }

   gl_Position = vec4(1.0);
}

