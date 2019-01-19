#!bin/python

'''
#solr_find.py

called by listmanager_term.c or some variant thereof
purpose is to query the solr db for a set of search terms
this seemed easier in python although presumably
could have been done in c directly with http requests
and may do that at some point.  This was a good excuse
to mix c and python.

need to do two main things for this to work:

1. export PYTHONPATH="/home...." [directory listmanager is running from]
2. source bin/activate python virtual environment [to have access to SolrClient]

A few key lines in the c code:

1. int len = PyList_Size(pValue);
2. using PyUnicode_AsUTF8AndSize(PyList_GetItem(pValue, i), &size) as below:

  for (int i=0; i<len;i++) {
    put += snprintf(put, sizeof(query) - (put - query), "%s, ", PyUnicode_AsUTF8AndSize(PyList_GetItem(pValue, i), &size));
  }

'''

from SolrClient import SolrClient
from config import SOLR_URI
solr = SolrClient(SOLR_URI + '/solr')
collection = 'listmanager'

def solr_find(search_terms):
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
    return solr_ids 

