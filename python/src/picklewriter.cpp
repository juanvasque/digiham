#include "picklewriter.hpp"

#include <Python.h>
#include <cstring>

using namespace Digiham;

void PickleWriter::sendMetaData(std::map<std::string, std::string> metadata) {
    // acquire GIL
    PyGILState_STATE gstate;
    gstate = PyGILState_Ensure();

    PyObject* pickle = PyImport_ImportModule("pickle");
    if (pickle == NULL) throw std::runtime_error("importing pickle module failed");

    PyObject* dict = PyDict_New();
    if (dict == NULL) {
        Py_DECREF(pickle);
        throw std::runtime_error("creating dictionary failed");
    }

    for (auto entry: metadata) {
        PyObject* value = PyUnicode_FromStringAndSize(entry.second.c_str(), entry.second.length());
        if (value == NULL) {
            Py_DECREF(pickle);
            Py_DECREF(dict);
            throw std::runtime_error("failed to create python string object");
        }
        if (PyDict_SetItemString(dict, entry.first.c_str(), value) == -1) {
            Py_DECREF(pickle);
            Py_DECREF(dict);
            Py_DECREF(value);
            throw std::runtime_error("failed to put item into dictionary");
        }

        Py_DECREF(value);
    }

    PyObject* result = PyObject_CallMethod(pickle, "dumps", "O", dict);
    if (result == NULL) {
        Py_DECREF(pickle);
        Py_DECREF(dict);
        throw std::runtime_error("failed to pickle dictionary");
    }

    Py_DECREF(dict);
    Py_DECREF(pickle);

    if (!PyBytes_Check(result)) {
        Py_DECREF(result);
        throw std::runtime_error("unexpected result (was expecting bytes)");
    }

    char* bytes;
    Py_ssize_t length;
    if (PyBytes_AsStringAndSize(result, &bytes, &length) == -1) {
        Py_DECREF(result);
        throw std::runtime_error("failed to extract result bytes");
    }

    if (writer->writeable() < (size_t) length) return;

    std::memcpy(writer->getWritePointer(), bytes, length);
    // no need to compensate for multi-byte types since we only extend Writer<unsigned char>
    writer->advance(length);

    Py_DECREF(result);

    /* Release the thread. No Python API allowed beyond this point. */
    PyGILState_Release(gstate);
}