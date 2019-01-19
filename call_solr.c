#include <Python.h>
/*
this program was used to demonstrate that it was possible to have
c program access solr by calling a python script that did
a solr search and returned the postgres ids of the search results
after receiving the ids from solr this c program constructs
the sql query that will retrieve the search result set
from the listmanager db
the python program was quite simple and in it's entirety is:


#solr.py
from SolrClient import SolrClient
#import requests
from config import SOLR_URI
solr = SolrClient(SOLR_URI + '/solr')
collection = 'listmanager'

def get_ids(search_terms):
    s0 = search_terms.split()
    s1 = 'title:' + ' OR title:'.join(s0)
    s2 = 'note:' + ' OR note:'.join(s0)
    s3 = 'tag:' + ' OR tag:'.join(s0)
    q = s1 + ' OR ' + s2 + ' OR ' + s3
    result = solr.query(collection, {
                'q':q, 'rows':50, 'fl':['score', 'id', 'title', 'tag', 'star', 
                'context', 'completed'], 'sort':'score desc'})
    items = result.docs
    solr_ids = [x['id'] for x in items]
    print(solr_ids)
    return solr_ids 
*/

int
main(int argc, char *argv[])
{
    PyObject *pName, *pModule, *pFunc;
    PyObject *pArgs, *pValue;
    //int i;

    if (argc < 3) {
        fprintf(stderr,"Usage: call pythonfile funcname [args]\n");
        return 1;
    }

    Py_Initialize();
    pName = PyUnicode_DecodeFSDefault(argv[1]);
    /* Error checking of pName left out */

    pModule = PyImport_Import(pName);
    Py_DECREF(pName);

    if (pModule != NULL) {
        pFunc = PyObject_GetAttrString(pModule, argv[2]);
        /* pFunc is a new reference */

        if (pFunc && PyCallable_Check(pFunc)) {
            pArgs = PyTuple_New(1); //presumably PyTuple_New(x) creates a tuple with that many elements
            pValue = Py_BuildValue("s", argv[3]); // **************
            PyTuple_SetItem(pArgs, 0, pValue); // ***********
            pValue = PyObject_CallObject(pFunc, pArgs);
                if (!pValue) {
                    Py_DECREF(pArgs);
                    Py_DECREF(pModule);
                    fprintf(stderr, "Cannot convert argument\n");
                    return 1;
            }
            Py_DECREF(pArgs);
            if (pValue != NULL) {
                printf("Length of list: %d\n", PyList_Size(pValue));
                Py_ssize_t size; 
                printf("Third item of list: %s\n", PyUnicode_AsUTF8AndSize(PyList_GetItem(pValue, 2), &size));
                int len = PyList_Size(pValue);
                //int solr_ids[100]
                int solr_ids[len];

                for (int i=0; i<len;i++) {
                  solr_ids[i] = atoi(PyUnicode_AsUTF8AndSize(PyList_GetItem(pValue, i), &size));
                }

                printf("\n Below is the print from c\n");
                for (int i=0; i<len;i++) {
                  printf(" %d", solr_ids[i]);
                }


              /*
              We want to create a query that looks like:
              SELECT * FROM task 
              WHERE task.id IN (1234, 5678, , 9012) 
              ORDER BY task.id = 1234 DESC, task.id = 5678 DESC, task.id = 9012 DESC
              */


                char query[2000];
                char *put;
                strncpy(query, "SELECT * FROM task WHERE task.id IN (", sizeof(query));
                put = &query[strlen(query)];

                for (int i=0; i<len;i++) {
                  put += snprintf(put, sizeof(query) - (put - query), "%s, ", PyUnicode_AsUTF8AndSize(PyList_GetItem(pValue, i), &size));
                }

                int slen = strlen(query);
                query[slen-2] = ')'; //have extra comma space and need closing paren
                query[slen-1] = '\0';

                put = &query[strlen(query)];
                put += snprintf(put, sizeof(query) - (put - query), "%s", " ORDER BY ");

                for (int i=0; i<len;i++) {
                  put += snprintf(put, sizeof(query) - (put - query), "task.id = %s DESC, ", PyUnicode_AsUTF8AndSize(PyList_GetItem(pValue, i), &size));
                }

                slen = strlen(query);
                query[slen-2] = '\0'; //have extra comma space

                printf("\n\n\n%s", query);

                //printf("Result of call: %ld\n", PyLong_AsLong(pValue));
                Py_DECREF(pValue);
            }
            else {
                Py_DECREF(pFunc);
                Py_DECREF(pModule);
                PyErr_Print();
                fprintf(stderr,"Call failed\n");
                return 1;
            }
        }
        else {
            if (PyErr_Occurred())
                PyErr_Print();
            fprintf(stderr, "Cannot find function \"%s\"\n", argv[2]);
        }
        Py_XDECREF(pFunc);
        Py_DECREF(pModule);
    }
    else {
        PyErr_Print();
        fprintf(stderr, "Failed to load \"%s\"\n", argv[1]);
        return 1;
    }
    if (Py_FinalizeEx() < 0) {
        return 120;
    }
    return 0;
}
