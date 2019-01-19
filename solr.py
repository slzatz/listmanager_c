#!bin/python

'''This works with call_solr.c

  ./call_solr solr get_ids micropython

  call_solr was compiled with:

  gcc call_solr.c -I/usr/include/python3.7m -L/usr/include/python3.7m -lpython3.7m -o call_solr2

  There is also a solr_sqla.py created to look at the sql after you retrieve the solr ids

'''


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

