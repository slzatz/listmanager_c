#!bin/python

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

