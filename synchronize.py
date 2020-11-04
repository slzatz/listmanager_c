#!bin/python

'''
module to sync remote sql-based server (currently postgreSQL) database with a local (currently sqlite) database
'''

import datetime
from lmdb_s import * 
import lmdb_p as p
import sqlite3

def synchronize(report_only=True):
    nn = 0
    log = "****************************** BEGIN SYNC *******************************************\n\n"

    # below for updating fts db: see server updated tasks
    fts = sqlite3.connect("fts5.db")

    remote_session = p.remote_session
    try:
        remote_session.execute("SELECT 1")
    except sqla_exc.OperationalError as e: 
        log+= "Could not establish a remote session with the PG database"
        return 0
        
    # The last sync is stored on client since each client will have a different server and client sync time    
    client_sync = local_session.query(Sync).get('client') 
    server_sync = local_session.query(Sync).get('server') 

    last_client_sync = client_sync.timestamp 
    last_server_sync = server_sync.timestamp 

    log+= "LISTMANAGER SYNCRONIZATION\n"
    log+= "Server you are synching with is {}\n".format(p.remote_engine)
    log+= "Local Time is {0}\n\n".format(datetime.datetime.now())
    log+= "UTC Time is {0}\n\n".format(datetime.datetime.utcnow())
    delta = datetime.datetime.utcnow() - last_client_sync
    log+= "The last time client was synced (based on client clock) was {}, which was {} days and {} minutes ago.\n".format(last_client_sync.isoformat(' ')[:19], delta.days, delta.seconds/60)
    log+= "The last time server was synced (based on server clock) was {}, which was {} days and {} minutes ago.\n".format(last_server_sync.isoformat(' ')[:19], delta.days, delta.seconds/60)

    # SERVER CHANGES
    #server_updated_contexts: new and modified
    server_updated_contexts = remote_session.query(p.Context).filter(and_(
      p.Context.modified > last_server_sync, p.Context.deleted==False)).all()

    if server_updated_contexts:
        nn+=len(server_updated_contexts)
        log+=f"Updated (new and modified) server Contexts since the last sync: {len(server_updated_contexts)}.\n"
    else:
        log+="There were no updated (new and modified) server Contexts since the last sync.\n" 

    #server_deleted_contexts
    server_deleted_contexts = remote_session.query(p.Context).filter(and_(
      p.Context.modified > last_server_sync, p.Context.deleted==True)).all()
      
    if server_deleted_contexts:
        nn+=len(server_deleted_contexts)
        log+=f"Deleted server Contexts since the last sync: {len(server_deleted_contexts)}.\n"
    else:
        log+="There were no server Contexts deleted since the last sync.\n" 

    #server_updated_folders: new and modified
    server_updated_folders = remote_session.query(p.Folder).filter(and_(
      p.Folder.modified > last_server_sync, p.Folder.deleted==False)).all()

    if server_updated_folders:
        nn+=len(server_updated_folders)
        log+=f"Updated (new and modified) server Folders since the last sync: {len(server_updated_folders)}.\n"
    else:
        log+="There were no updated (new and modified) server Folders since the last sync.\n" 

    #server_deleted_folders
    server_deleted_folders = remote_session.query(p.Folder).filter(and_(
      p.Folder.modified > last_server_sync, p.Folder.deleted==True)).all()
      
    if server_deleted_folders:
        nn+=len(server_deleted_folders)
        log+=f"Deleted server Folders since the last sync: {len(server_deleted_folders)}.\n"
    else:
        log+="There were no server Folders deleted since the last sync.\n" 

    #server_updated_keywords: new and modified
    server_updated_keywords = remote_session.query(p.Keyword).filter(and_(
      p.Keyword.modified > last_server_sync, p.Keyword.deleted==False)).all()

    if server_updated_keywords:
        nn+=len(server_updated_keywords)
        log+=f"Updated (new and modified) server Keywords since the last sync: {len(server_updated_keywords)}.\n"
    else:
        log+="There were no updated (new and modified) server Keywords since the last sync.\n" 

    #server_deleted_keywords
    server_deleted_keywords = remote_session.query(p.Keyword).filter(and_(
      p.Keyword.modified > last_server_sync, p.Keyword.deleted==True)).all()
      
    if server_deleted_keywords:
        nn+=len(server_deleted_keywords)
        log+=f"Deleted server Keywords since the last sync: {len(server_deleted_keywords)}.\n"
    else:
        log+="There were no server Keywords deleted since the last sync.\n" 

    #server_updated_tasks: new and modified
    server_updated_tasks = remote_session.query(p.Task).filter(and_(
      p.Task.modified > last_server_sync, p.Task.deleted==False, p.Task.id > 1)).all()

    if server_updated_tasks:
        nn+=len(server_updated_tasks)
        log+="Updated (new and modified) server Tasks since the last sync: {0}.\n".format(len(server_updated_tasks))
    else:
        log+="There were no updated (new and modified) server Tasks since the last sync.\n" 

    #server_deleted_tasks
    server_deleted_tasks = remote_session.query(p.Task).filter(and_(
      p.Task.modified > last_server_sync, p.Task.deleted==True)).all()
      
    if server_deleted_tasks:
        nn+=len(server_deleted_tasks)
        log+="Deleted server Tasks since the last sync: {0}.\n".format(len(server_deleted_tasks))
    else:
        log+="There were no server Tasks deleted since the last sync.\n" 

    log+="\nThe total number of server postgresql changes is {0}.\n\n".format(nn)

    # CLIENT CHANGES
    #client_updated_contexts: new and modified
    client_updated_contexts = local_session.query(Context).filter(and_(
      Context.modified > last_client_sync, Context.deleted==False)).all()

    if client_updated_contexts:
        nn+=len(client_updated_contexts)
        log+=f"Updated (new and modified) client Contexts since the last sync: {len(client_updated_contexts)}.\n"
    else:
        log+="There were no updated (new and modified) client Contexts since the last sync.\n" 

    #client_deleted_contexts
    client_deleted_contexts = local_session.query(Context).filter(and_(
      Context.modified > last_client_sync, Context.deleted==True)).all()
      
    if client_deleted_contexts:
        nn+=len(client_deleted_contexts)
        log+=f"Deleted client Contexts since the last sync: {len(client_deleted_contexts)}.\n"
    else:
        log+="There were no client Contexts deleted since the last sync.\n" 

    #client_updated_folders: new and modified
    client_updated_folders = local_session.query(Folder).filter(and_(
      Folder.modified > last_client_sync, Folder.deleted==False)).all()

    if client_updated_folders:
        nn+=len(client_updated_folders)
        log+=f"Updated (new and modified) client Folders since the last sync: {len(client_updated_folders)}.\n"
    else:
        log+="There were no updated (new and modified) client Folders since the last sync.\n" 

    #client_deleted_folders
    client_deleted_folders = local_session.query(Folder).filter(and_(
      Folder.modified > last_client_sync, Folder.deleted==True)).all()
      
    if client_deleted_folders:
        nn+=len(client_deleted_folders)
        log+=f"Deleted client Folders since the last sync: {len(client_deleted_folders)}.\n"
    else:
        log+="There were no client Folders deleted since the last sync.\n" 

    #client_updated_keywords: new and modified
    client_updated_keywords = local_session.query(Keyword).filter(and_(
      Keyword.modified > last_client_sync, Keyword.deleted==False)).all()

    if client_updated_keywords:
        nn+=len(client_updated_keywords)
        log+=f"Updated (new and modified) client Keywords since the last sync: {len(client_updated_keywords)}.\n"
    else:
        log+="There were no updated (new and modified) client Keywords since the last sync.\n" 

    #client_deleted_keywords
    client_deleted_keywords = local_session.query(Keyword).filter(and_(
      Keyword.modified > last_client_sync, Keyword.deleted==True)).all()
      
    if client_deleted_keywords:
        nn+=len(client_deleted_keywords)
        log+=f"Deleted client Keyword since the last sync: {len(client_deleted_keywords)}.\n"
    else:
        log+="There were no client Keyword deleted since the last sync.\n" 

    #client_updated_tasks: new and modified 
    client_updated_tasks = local_session.query(Task).filter(and_(
    Task.modified > last_client_sync, Task.deleted==False)).all()

    if client_updated_tasks:
        nn+=len(client_updated_tasks)
        log+=f"Updated (new and modified) client Tasks since the last sync: {len(client_updated_tasks)}.\n"
    else:
        log+="There were no updated (new and modified) client Tasks since the last sync.\n" 

    #client_deleted_tasks
    client_deleted_tasks = local_session.query(Task).filter(Task.deleted==True).all()
    if client_deleted_tasks:
        nn+=len(client_deleted_tasks)
        log+="Deleted client Tasks since the last sync: {0}.\n".format(len(client_deleted_tasks))
    else:
        log+="There were no client Tasks deleted since the last sync.\n" 

    log+="\nThe total number of server and client changes is {0}.\n".format(nn)

    if report_only:
        with open('log', 'w') as f:
            f.write(log)
        return nn

    # updated server contexts -> client
    if server_updated_contexts:
        log += "\nContexts that were updated/created on the Server that need to be updated/created on the Client:\n"
    for sc in server_updated_contexts:

        context = local_session.query(Context).filter_by(tid=sc.id).first()

        if not context:
            action = "created"
            context = Context(sc.id, sc.title) # Context(tid, title)
            local_session.add(context)
        else:
            action = "updated"
            context.title = sc.title
            
        # Note that the foreign key that the server uses task.context_tid points to context.id is different
        # between postgres and sqlite postgres points to context.id and local
        # sqlite db points to context.tid but the actual values are identical
        context.default = sc.default # -> row.star; sqla knows this is "default"
        context.textcolor = sc.textcolor

        local_session.commit() #new/updated client task commit
        
        log += f"{action} - id: {context.id} tid: {context.tid} title: {context.title} \n"
        
    # updated client contexts -> server
    if client_updated_contexts:
        log += "\nContexts that were updated/created on Client that need to be updated/created on Server:\n"
    for cc in client_updated_contexts:

        context = remote_session.query(p.Context).filter_by(id=cc.tid).first()

        if not context:
            action = "created"
            context = p.Context(cc.title)
            remote_session.add(context)
            remote_session.commit()
            cc.tid = context.id 
            local_session.commit()
        else:
            action = "updated"
            if context in server_updated_contexts:
                action += "-server won"
                continue
            
            context.title = cc.title

        context.default = cc.default # -> row.star; sqla knows this is "default"
        context.textcolor = cc.textcolor

        remote_session.commit()

        log += f"{action} - id: {context.id} title: {context.title} \n"

    # updated server folders -> client
    if server_updated_folders:
        log += "\nFolders that were updated/created on the Server that need to be updated/created on the Client:\n"
    for sf in server_updated_folders:

        folder = local_session.query(Folder).filter_by(tid=sf.id).first()

        if not folder:
            action = "created"
            #Folder(tid, title)
            folder = Folder(sf.id, sf.title)
            local_session.add(folder)
            # next line important: local db task.tid is the unique key that 
            # links to the foreign key in context and folder tables
            #folder.tid = sf.id
        else:
            action = "updated"
            folder.title = sf.title
            
        # Note that the foreign key that the server uses task.context_tid points to context.id is different
        # between postgres and sqlite postgres points to context.id and local
        # sqlite db points to context.tid but the actual values are identical
        #folder.title = sf.title
        folder.private = sf.private # -> row.star
        folder.textcolor = sf.textcolor

        local_session.commit() #new/updated client folder commit
        
        log += f"{action} - id: {folder.id} tid: {folder.tid} title: {folder.title} \n"
        
    # updated client folders -> server
    if client_updated_folders:
        log += "\nFolders that were updated/created on Client that need to be updated/created on Server:\n"
    for cf in client_updated_folders:

        # note for a new context/folder/keyword this cf/cc/ck.tid is set = 0
        folder = remote_session.query(p.Folder).filter_by(id=cf.tid).first()

        # the below works great for new [folder/context/keyword] but not for updating [name/title]
        #folder = remote_session.query(p.Folder).filter_by(title=cf.title).first()

        if not folder:
            action = "created"
            folder = p.Folder(cf.title) # needs a title but will be changed below
            remote_session.add(folder)
            remote_session.commit()
            cf.tid = folder.id 
            local_session.commit()
        else:
            action = "updated"
            if folder in server_updated_folders:
                action += "-server won"
                continue
            
            folder.title = cf.title

        folder.private = cf.private # -> row.star
        folder.textcolor = cf.textcolor

        remote_session.commit()

        log += f"{action} - id: {folder.id} title: {folder.title} \n"
    #######################################################################################
    #updated server keywords -> client
    if server_updated_keywords:
        log += "\nKeywords that were updated/created on the Server that need to be updatd/dreated on the Client:\n"
    for sk in server_updated_keywords:

        keyword = local_session.query(Keyword).filter_by(tid=sk.id).first()

        if not keyword:
            action = "created"
            #Keyword(tid, name)
            keyword = Keyword(sk.id, sk.name)
            local_session.add(keyword)
            #keyword.tid = sk.id
        else:
            action = "updated"
            keyword.name = sk.name

        keyword.star = sk.star
        # modified should be handled by db

        local_session.commit()

        log += f"{action} - id: {keyword.id} tid: {keyword.tid} title: {keyword.name} \n"

    # updated client keywords -> server
    if client_updated_keywords:
        log += "\nKeywords that were updated/created on Client that need to be updated/created on Server:\n"
    for ck in client_updated_keywords:

        # means you can only do one new one at a time since both get same bogus tid unless you increment it each time
        # I think the above is wrong - at least for keywords, it doesn't matter if three new keywords get the same placeholder tid = 0


        # could be duplicated keyword name that is deleted on server
        keyword = remote_session.query(p.Keyword).filter_by(id=ck.tid).first()
        dup_keyword = remote_session.query(p.Keyword).filter_by(name=ck.name).first()
        name = ck.name if not dup_keyword else ck.name+"_dup"   

        if not keyword:
            action = "created"
            keyword = p.Keyword(name)
            remote_session.add(keyword)
            remote_session.commit()
            ck.tid = keyword.id
            local_session.commit()
        else:
            action = "updated"
            if keyword in server_updated_keywords:
                action += "-server won"
                continue

            keyword.name = name
            remote_session.commit()

        keyword.star = ck.star
        #modified should be handled by db

        remote_session.commit()

        log += f"{action} - id: {keyword.id} title: {keyword.name} \n"
    #######################################################################################

    # updated server tasks -> client
    if server_updated_tasks:
        log += "\nTasks that were updated/created on the Server that need to be updated/created on the Client:\n"

    for st in server_updated_tasks:
        
        # to find the sqlite task that corresponds to the updated server task
        # you need to match the sqlite task.tid with the postgres task.id
        task = local_session.query(Task).filter_by(tid=st.id).first() #if you use one, you can get exception
        
        if not task:
            action = "created"
            task = Task()
            local_session.add(task)
            # next line important: local db task.tid is the unique key that 
            # links to the foreign key in context and folder tables
            task.tid = st.id
        else:
            action = "updated"
            
        # Note that the foreign key that context_tid points to is different
        # between postgres and sqlite: postgres points to context.id and local
        # sqlite db points to context.tid; the pg and sqlite task.context_tid values
        # are the same 
        task.context_tid = st.context_tid
        task.duedate = st.duedate
        task.duetime = st.duetime if st.duetime else None #########
        task.remind = st.remind
        #task.startdate = st.startdate if st.startdate else st.added ################ may 2, 2012
        task.startdate = st.startdate #changed from above on 09092020 to use startdate as note changed date
        task.folder_tid = st.folder_tid 
        task.title = st.title if st.title else '' # somehow st.title can be None and needs to be a string
        task.added = st.added
        task.star = st.star
        task.priority = st.priority
        task.tag = st.tag
        task.completed = st.completed if st.completed else None
        task.note = st.note
        task.subnote = st.subnote
        #task.modified = st.modified 
        task.modified = datetime.datetime.utcnow()

        local_session.commit() #new/updated client task commit

        log += f"{action} - id: {task.id} tid: {task.tid}; star: {task.star}; priority: {task.priority}; completed: {task.completed}; title: {task.title[:32]}\n"
        # ? could be an delete query
        #if task.tag:
        #local_session.query(TaskKeyword).filter(TaskKeyword.task_id = task.id).delete(synchronize_session=False)
        for tk in task.taskkeywords:
            local_session.delete(tk)
        local_session.commit()

        for kw in st.keywords:
        #for kwn in task.tag.split(','):
            keyword = local_session.query(Keyword).filter_by(name=kw.name).first()
            #keyword = local_session.query(Keyword).filter_by(name=kwn).first()
            if keyword is None:
                #keyword = Keyword(kwn)
                keyword = Keyword(kw.name)
                local_session.add(keyword)
                local_session.commit()
            tk = TaskKeyword(task,keyword)
            local_session.add(tk)

        local_session.commit()
            
        if action == "created":
            title = task.title
            note = task.note # not indexing subnote
            if not note:
                note = ""
            # not sure escaping the single quotes is necessary here since probably already escaped but not sure
            title = title.replace("'", "''")
            note = note.replace("'", "''")
            tag = ",".join(kw.name for kw in task.keywords)
            # I don't think you can do INSERT OR IGNORE on a table without a unique contraint so could do a select if
            # want to avoid duplicate entries. Would have to be two separate statements since sqlite doesn't support
            # IF NOT EXISTS (SELECT 1 from fts WHERE lm_id = whatever)) INSERT INTO ...
            result = fts.execute(f"INSERT INTO fts (title, note, tag, lm_id) VALUES (\'{title}\', \'{note}\', \'{tag}\' ,{task.id});")
            fts.commit() # this is needed!!!!!
            if result.rowcount != 1:
                log+= f"Somethere went wrong adding to fts db: {task.title}, id: {task.id} tid: {task.tid}"
                  

    if client_updated_tasks:
        log += "\nTasks that were updated/created on the Client that need to be updated/created on the Server:\n"

    for ct in client_updated_tasks:
        
        # to find the postgres task that corresponds to the updated client task you need to match the sqlite task.tid with the 
        # postgres task.id
        task = remote_session.query(p.Task).filter_by(id=ct.tid).first() # could also do task.get(ct.tid)
        
        if not task:
            action = "created"
            task = p.Task()
            remote_session.add(task)
            remote_session.commit()
            ct.tid = task.id
            local_session.commit()
        else:
            action = "updated"
            if task in server_updated_tasks:
                action += "-server won"
                continue

        task.context_tid = ct.context_tid
        task.duedate = ct.duedate
        task.duetime = ct.duetime if ct.duetime else None #########
        task.remind = ct.remind
        #task.startdate = ct.startdate if ct.startdate else ct.added ################ may 2, 2012
        task.startdate = ct.startdate #changed from above on 09092020 to use startdate as note changed date
        task.folder_tid = ct.folder_tid 
        task.title = ct.title
        task.added = ct.added
        task.star = ct.star
        task.priority = ct.priority
        task.tag = ct.tag
        task.completed = ct.completed if ct.completed else None
        task.note = ct.note
        task.subnote = ct.subnote
        #task.modified = ct.modified # not necessary - done by sqla onupdate but might be if no changes detected ie keywords change
        task.modified = datetime.datetime.utcnow()

        remote_session.commit() #new/updated client task commit

        log += f"{action} - id: {task.id}; star: {task.star}; priority: {task.priority}; completed: {task.completed}; title: {task.title[:32]}\n"

        # ? could be an delete query
        #remote_session.query(p.TaskKeyword).filter(p.TaskKeyword.task_id = task.id).delete(synchronize_session=False)
        #if task.tag:
        for tk in task.taskkeywords:
            remote_session.delete(tk)
        remote_session.commit()

        for kw in ct.keywords:
            # the theory is that this should always match but we do check below and log an error if no remote keyword names matches client kw
            keyword = remote_session.query(p.Keyword).filter_by(name=kw.name).first()

           # no longer makes sense to create keyword here since unconnected by tid to the client keyword
            if keyword is None:
                log += f"Error: can't find keyword {kw.name} on the server\n"
           #     keyword = p.Keyword(kw.name)
           #     remote_session.add(keyword)
           #     remote_session.commit()
            else:
                tk = p.TaskKeyword(task,keyword)
                remote_session.add(tk)

        remote_session.commit()
            
    # Delete from client tasks deleted on server
    for t in server_deleted_tasks:

        # The for loop above could be an sql update query, I believe
        ##########################################################################################
        #stmt = p.task_table.update().where(p.task_table.c.context_tid==sc.id) values(context_tid=1)
        #stmt = task_table.delete().task_table.id.in_(1,2)
        #result = remote_conn.execute(stmt)
        ##########################################################################################

        # delete remote keywords
        # sql: DELETE FROM task_keyword WHERE task_id = t.id
        #remote_session.query(p.TaskKeyword).filter(p.TaskKeyword.task_id = t.id).delete(synchronize_session=False)
        for tk in t.taskkeywords:
            remote_session.delete(tk)
        remote_session.commit()

        task = local_session.query(Task).filter_by(tid=t.id).first()
        if task:
                    
            log+=f"Task deleted on Server deleted task on Client - id: {task.id}; tid: {task.tid}; title: {task.title[:32]}\n"
            
            # ? could be an delete query
            #local_session.query(TaskKeyword).filter(TaskKeyword.task_id = task.id).delete(synchronize_session=False)
            for tk in task.taskkeywords:
                local_session.delete(tk)
            local_session.commit()
        
            local_session.delete(task)
            local_session.commit() 
            
        else:
            
            log+=f"Task deleted on Server unsuccessful trying to delete on Client - could not find Client Task with tid = {t.id}\n"

    # uses deletelist 
    # Delete from server tasks deleted on client
    for t in client_deleted_tasks:

        # The for loop above could be an sql update query, I believe
        ##########################################################################################
        #stmt = p.task_table.update().where(p.task_table.c.context_tid==sc.id) values(context_tid=1)
        #stmt = p.task_table.update().task_table.id.in_(1,2).values(deleted=True)
        #result = remote_conn.execute(stmt)
        ##########################################################################################

        # need 'if' below because a task could be new and then deleted and therefore have not tid; 
        # it will be removed from client but can't send anything to server
        if t.tid:
            try:
                task = remote_session.query(p.Task).get(t.tid)
            except Exception as e:
                log+= f"Exception {e} is 'for t in client_deleted_tasks'"
            else:
                task.deleted = True
                remote_session.commit()

                # delete remote keywords
                #remote_session.query(p.TaskKeyword).filter(p.TaskKeyword.task_id = task.id).delete(synchronize_session=False)
                for tk in task.taskkeywords:
                    remote_session.delete(tk)
                remote_session.commit()
                
        # delete local keywords
        #local_session.query(TaskKeyword).filter(TaskKeyword.task_id = t.id).delete(synchronize_session=False)
        for tk in t.taskkeywords:
            local_session.delete(tk)
        local_session.commit()
                
        local_session.delete(t)
        local_session.commit()     
         
    # Delete from client contexts deleted on server
    for sc in server_deleted_contexts:
        # deal with server context's related tasks    
        remote_session.query(p.Task).filter(p.Task.context_tid==sc.id).update({p.Task.context_tid : 1}) #? synchronize_session =   False
        remote_session.commit()

        # deal with client context and its related tasks    
        context = local_session.query(Context).filter_by(tid=sc.id).first()
        if context:
            log+=f"Context deleted on Server will be deleted on Client - id: {context.id}; tid: {context.tid}; title: {context.title}\n"

            local_session.query(Task).filter(Task.context_tid==sc.id).update({Task.context_tid : 1}) #, synchronize_session=
            local_session.commit()
            local_session.delete(context)
            local_session.commit() 
        else:
            log+="Context deleted on Server unsuccessful trying to delete on Client - could not find Client context with tid = {sc.id}\n"

    # Delete from server contexts deleted on client
    for cc in client_deleted_contexts:
        # deal with client context's related tasks    
        local_session.query(Task).filter(Task.context_tid==cc.tid).update({Task.context_tid : 1})
        local_session.commit()

        # deal with server context and its related tasks    
        context = remote_session.query(p.Context).filter_by(id=cc.tid).first()
        if context:
            log+=f"Context deleted on Client will be marked as deleted on Server - id: {context.id}; title: {context.title}\n"

            remote_session.query(p.Task).filter(p.Task.context_tid==cc.tid).update({p.Task.context_tid : 1}) #? synchronize_session =   False
            remote_session.commit()
            context.deleted = True
            remote_session.commit()
        else:
            log+="Context deleted on Client unsuccessful trying to delete on Server - could not find Server context with id = {cc.tid}\n"

        local_session.delete(cc)
        local_session.commit()

    # Delete from client folders deleted on server
    for sf in server_deleted_folders:
        # deal with server folder's related tasks    
        remote_session.query(p.Task).filter(p.Task.folder_tid==sf.id).update({p.Task.folder_tid : 1}) #? synchronize_session =   False
        remote_session.commit()

        # deal with client folder and its related tasks    
        folder = local_session.query(Folder).filter_by(tid=sf.id).first()
        if folder:
            log+=f"Folder deleted on Server deleted on Client - id: {folder.id}; tid: {folder.tid}; title: {folder.title}\n"

            local_session.query(Task).filter(Task.folder_tid==sf.id).update({Task.folder_tid : 1}) #, synchronize_session=
            local_session.commit()
            local_session.delete(folder)
            local_session.commit() 
        else:
            log+="Folder deleted on Server unsuccessful trying to delete on Client - could not find Client folder with tid = {sf.id}\n"

    # Delete from server folders deleted on client
    for cf in client_deleted_folders:
        # deal with client folder's related tasks    
        # could do this immediately in listmanager but that could cause lots of task changes (? modified would be updated)
        local_session.query(Task).filter(Task.folder_tid==cf.tid).update({Task.folder_tid : 1})
        local_session.commit()

        # deal with server folder and its related tasks    
        folder = remote_session.query(p.Folder).filter_by(id=cf.tid).first()
        if folder:
            log+=f"Folder deleted on Client will be marked as deleted on Server - id: {folder.id}; title: {folder.title}\n"

            remote_session.query(p.Task).filter(p.Task.folder_tid==cf.tid).update({p.Task.folder_tid : 1}) #? synchronize_session =   False
            remote_session.commit()
            folder.deleted = True
            remote_session.commit() 
        else:
            log+="Folder deleted on Client unsuccessful trying to delete on Server - could not find Server folder with id = {cf.tid}\n"

        local_session.delete(cf)
        local_session.commit()
    ###############################################################################################################################

    # Delete from client keywords deleted on server
    for sk in server_deleted_keywords:
        # deal with server keywords's related task_keywords    
        remote_session.query(p.TaskKeyword).filter(p.TaskKeyword.keyword_id==sk.id).delete(synchronize_session=False)
        remote_session.commit()

        # deal with client keyword and its related task_keywords    
        keyword = local_session.query(Keyword).filter_by(tid=sk.id).first()
        if keyword:
            log+=f"Keyword deleted on Server deleted on Client - id: {keyword.id}; tid: {keyword.tid}; title: {keyword.name}\n"

            #local_session.query(TaskKeyword).filter(TaskKeyword.keyword_id==sk.id).delete(synchronize_session=False)
            local_session.query(TaskKeyword).filter(TaskKeyword.keyword_id==keyword.id).delete(synchronize_session=False)
            local_session.commit()
            local_session.delete(keyword)
            local_session.commit() 
        else:
            log+=f"Keyword deleted on Server unsuccessful trying to delete on Client - could not find Client keyword with tid = {sk.id}\n"

    # Delete from server keywords deleted on client
    for ck in client_deleted_keywords:
        # deal with client keywords's related task_keywords - note that this could be done in application at time of deleting   
        local_session.query(TaskKeyword).filter(TaskKeyword.keyword_id==ck.id).delete(synchronize_session=False)
        local_session.commit()

        # deal with server keyword and its related task_keywords    
        keyword = remote_session.query(p.Keyword).filter_by(id=ck.tid).first()
        if keyword:
            log+=f"Keyword deleted on Client will be marked as deleted on Server - id: {keyword.id}; title: {keyword.title}\n"

            # two lines below should be the same but not symmetrical when it comes to deleting local task_keywords above
            #remote_session.query(p.TaskKeyword).filter(p.TaskKeyword.keyword_id==ck.tid).delete(synchronize_session=False) 
            remote_session.query(p.TaskKeyword).filter(p.TaskKeyword.keyword_id==keyword.id).delete(synchronize_session=False) 

            remote_session.commit()
            keyword.deleted = True
            remote_session.commit() 
        else:
            log+="Keyword deleted on Client unsuccessful trying to delete on Server - could not find Server keyword with id = {ck.tid}\n"

        local_session.delete(ck)
        local_session.commit()
    ###############################################################################################################################
    # note that if we want to find unused keywords, this query does it:  SELECT name from keyword WHERE keyword.id NOT IN (SELECT keyword_id from task_keyword)


    client_sync.timestamp = datetime.datetime.utcnow() + datetime.timedelta(seconds=5) # giving a little buffer if the db takes time to update on client or server

    # saw definitively that the resulting timestamp could be earlier than when the server tasks were modified -- no idea why
    #connection = p.remote_engine.connect()
    #result = connection.execute("select extract(epoch from now())")
    #server_sync.timestamp = datetime.datetime.fromtimestamp(result.scalar()) + datetime.timedelta(seconds=10) # not sure why this is necessary but it really is

    # if you thought server and client sync times were close enough you wouldn't have to do this
    # and would have just one sync time
    #task = remote_session.query(p.Task).get(1) #Call Spectacles ...
    #priority = task.priority
    #task.priority = priority + 1 if priority < 3 else 0
    #remote_session.commit()
    #server_sync.timestamp = task.modified + datetime.timedelta(seconds=5) # not sure why this is necessary but it really is
    server_sync.timestamp = remote_session.execute("SELECT now()").fetchone()[0] + datetime.timedelta(seconds=5)

    local_session.commit()  

    log+="New Sync times\n"
    log+="Client UTC timestamp: {}\n".format(client_sync.timestamp.isoformat(' '))
    log+="Server UTC timestamp: {}\n".format(server_sync.timestamp.isoformat(' '))
    log+= "Local time is {0}\n\n".format(datetime.datetime.now())

    log+=("\n\n***************** END SYNC *******************************************")

    #return log,changes,tasklist,deletelist 
    with open('log', 'w') as f:
        f.write(log)

    return nn

if __name__ == "__main__":
    nn = synchronize(True)
    print(f"Number of items changed = {nn}")
