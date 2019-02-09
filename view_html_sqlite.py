#!bin/python

import sys
from lmdb_s import *
import tempfile
from subprocess import call
from bs4 import BeautifulSoup #########

meta_html = '''<meta name="viewport" content="width=device-width, initial-scale=1">
<link rel="stylesheet" href="/home/slzatz/Documents/github-markdown.css">
<style>
    .markdown-body {
        box-sizing: border-box;
        min-width: 200px;
        max-width: 980px;
        margin: 0 auto;
        padding: 45px;
    }

    @media (max-width: 767px) {
        .markdown-body {
            padding: 15px;
        }
}
</style>
'''

def view_html(task_id):
    #task_id = int(sys.argv[1])
    #print(sys.argv)
    #remote_session = new_remote_session()
    task = local_session.query(Task).get(task_id)

    note = task.note if task.note else ''

    with tempfile.NamedTemporaryFile(suffix=".tmp") as tf:
        #tf.write(note.encode("utf-8"))
        text = f"# {task.title} \n\n{note}"
        tf.write(text.encode('utf-8'))
        tf.flush()
        fn = tf.name
        call(['mkd2html', fn])
        html_fn  = fn[:fn.find('.')] + '.html'

    with open(html_fn, 'r+') as f:
        html_doc = f.read()
        soup = BeautifulSoup(html_doc, 'html.parser')
        while soup.head.meta:
            soup.head.meta.extract()
        # for some reason new meta needs to be calculated here
        new_meta = BeautifulSoup(meta_html, 'html.parser')
        soup.head.append(new_meta)
        tag = soup.body
        tag.name = 'article'
        tag ["class"] = 'markdown-body'
        f.seek(0)
        f.write(str(soup))
        f.truncate()

    # not sure how to eliminate error message
    #call(['chromium', '--single-process', html_fn]) # default is -new-tab
    #call(['chromium', html_fn]) # default is -new-tab
    call(['qutebrowser', html_fn]) # default is -new-tab


if __name__ == "__main__":
    view_html(4809)
