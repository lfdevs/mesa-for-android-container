# Copyright (C) 2014-2016 Intel Corporation.   All Rights Reserved.
#
# Permission is hereby granted, free of charge, to any person obtaining a
# copy of this software and associated documentation files (the "Software"),
# to deal in the Software without restriction, including without limitation
# the rights to use, copy, modify, merge, publish, distribute, sublicense,
# and/or sell copies of the Software, and to permit persons to whom the
# Software is furnished to do so, subject to the following conditions:
#
# The above copyright notice and this permission notice (including the next
# paragraph) shall be included in all copies or substantial portions of the
# Software.
#
# THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
# IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
# FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
# THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
# LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
# FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS
# IN THE SOFTWARE.

# Python source
from __future__ import print_function
import os
import errno
import sys
import argparse
from mako.template import Template
from mako.exceptions import RichTraceback


#==============================================================================
class MakoTemplateWriter:
    '''
        MakoTemplateWriter - Class (namespace) for functions to generate strings
        or files using the Mako template module.

        See http://docs.makotemplates.org/en/latest/ for
        mako documentation.
   '''
    
    @staticmethod
    def to_string(template_filename, **kwargs):
        '''
            Write template data to a string object and return the string
        '''
        from mako.template      import Template
        from mako.exceptions    import RichTraceback

        try:
            template = Template(filename=template_filename)
            # Split + Join fixes line-endings for whatever platform you are using
            return '\n'.join(template.render(**kwargs).splitlines())
        except:
            traceback = RichTraceback()
            for (filename, lineno, function, line) in traceback.traceback:
                print('File %s, line %s, in %s' % (filename, lineno, function))
                print(line, '\n')
            print('%s: %s' % (str(traceback.error.__class__.__name__), traceback.error))

    @staticmethod
    def to_file(template_filename, output_filename, **kwargs):
        '''
            Write template data to a file
        '''
        if not os.path.exists(os.path.dirname(output_filename)):
            try:
                os.makedirs(os.path.dirname(output_filename))
            except OSError as err:
                if err.errno != errno.EEXIST:
                    raise
        with open(output_filename, 'w') as outfile:
            print(MakoTemplateWriter.to_string(template_filename, **kwargs), file=outfile)


#==============================================================================
class ArgumentParser(argparse.ArgumentParser):
    '''
    Subclass of argparse.ArgumentParser

    Allow parsing from command files that start with @
    Example:
      >bt run @myargs.txt
    
    Contents of myargs.txt:
      -m <machine>
      --target cdv_win7
    
    The below function allows multiple args to be placed on the same text-file line.
    The default is one token per line, which is a little cumbersome.
    
    Also allow all characters after a '#' character to be ignored.
    '''
    
    #==============================================================================
    class _HelpFormatter(argparse.RawTextHelpFormatter):
        ''' Better help formatter for argument parser '''

        def _split_lines(self, text, width):
            ''' optimized split lines algorighm, indents split lines '''
            lines = text.splitlines()
            out_lines = []
            if len(lines):
                out_lines.append(lines[0])
                for line in lines[1:]:
                    out_lines.append('  ' + line)
            return out_lines

    #==============================================================================
    def __init__(self, *args, **kwargs):
        ''' Constructor.  Compatible with argparse.ArgumentParser(),
            but with some modifications for better usage and help display.
        '''
        super(ArgumentParser, self).__init__(
                *args,
                fromfile_prefix_chars='@',
                formatter_class=ArgumentParser._HelpFormatter,
                **kwargs)

    #==========================================================================
    def convert_arg_line_to_args(self, arg_line):
        ''' convert one line of parsed file to arguments '''
        arg_line = arg_line.split('#', 1)[0]
        if sys.platform == 'win32':
            arg_line = arg_line.replace('\\', '\\\\')
        for arg in shlex.split(arg_line):
            if not arg.strip():
                continue
            yield arg

    #==========================================================================
    def _read_args_from_files(self, arg_strings):
        ''' read arguments from files '''
        # expand arguments referencing files
        new_arg_strings = []
        for arg_string in arg_strings:

            # for regular arguments, just add them back into the list
            if arg_string[0] not in self.fromfile_prefix_chars:
                new_arg_strings.append(arg_string)

            # replace arguments referencing files with the file content
            else:
                filename = arg_string[1:]

                # Search in sys.path
                if not os.path.exists(filename):
                    for path in sys.path:
                        filename = os.path.join(path, arg_string[1:])
                        if os.path.exists(filename):
                            break

                try:
                    args_file = open(filename)
                    try:
                        arg_strings = []
                        for arg_line in args_file.read().splitlines():
                            for arg in self.convert_arg_line_to_args(arg_line):
                                arg_strings.append(arg)
                        arg_strings = self._read_args_from_files(arg_strings)
                        new_arg_strings.extend(arg_strings)
                    finally:
                        args_file.close()
                except IOError:
                    err = sys.exc_info()[1]
                    self.error(str(err))

        # return the modified argument list
        return new_arg_strings
