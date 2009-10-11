#include <Python.h>

PyMODINIT_FUNC
initblist(void);
int main(int argc, char** argv)
{
        //Initialize python
        Py_Initialize();


        initblist();

        //Get the main module
        PyRun_SimpleString("import sys");
        PyRun_SimpleString("sys.path.append('.')");
        PyRun_SimpleString("import fuzz");
        Py_Finalize();
        return 0;
}
