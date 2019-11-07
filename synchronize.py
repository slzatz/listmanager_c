#!bin/python

'''
module to sync remote sql-based server (currently postgreSQL) database with a local (currently sqlite) database
But confusingly the downloadtasksfromserver is about downloading from toodledo to sql-based server with
the additional factor that you want to run the sql-based server in front of the local database such that the
local server cannot synch directly with toodledo only the remote server can.
'''

import datetime
from lmdb_s import * 
import lmdb_p as p
#import lmglobals_s as g

def synchronize(report_only=True):
    '''
    This synch is designed to be between postgreSQL and sqlite.  It is not the synch between toodledo and postgreSQL
    '''
    nn = 0
    log = "****************************** BEGIN SYNC *******************************************\n\n"

    # may want to return a dictionary that identifies what might need further processing
    # "server_tasks", "client_tasks", "changes", "server_deleted", "client_deleted"
    # although solved the alarm issue by re-scanning whole db after synchs for alarms
    changes = [] #server changed context and folder
    tasklist= [] #server changed tasks
    deletelist = [] #server deleted tasks

    # Seems necessary to create the remote session when needed since it appears to close if unused
    # Have now added pool_recycle to create_engine so remote_session = p.remote_session may be OK
    remote_session = p.remote_session
    try:
        remote_session.execute("SELECT 1")
    except sqla_exc.OperationalError as e: 
        log+= "Could not establish a remote session with the PG database"
        return 0
        
    client_sync = local_session.query(Sync).get('client') 
    server_sync = local_session.query(Sync).get('server') 

    last_client_sync = client_sync.timestamp 
    last_server_sync = server_sync.timestamp 

    log+= "LISTMANAGER SYNCRONIZATION NORM\n"
    log+= "Server you are synching with is {}\n".format(p.remote_engine)
    log+= "Local Time is {0}\n\n".format(datetime.datetime.now())
    delta = datetime.datetime.now() - last_client_sync
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

    #server_updated_folders: new and modified
    server_updated_folders = remote_session.query(p.Folder).filter(and_(
      p.Folder.modified > last_server_sync, p.Folder.deleted==False)).all()

    if server_updated_folders:
        nn+=len(server_updated_folders)
        log+=f"Updated (new and modified) server Folders since the last sync: {len(server_updated_folders)}.\n"
    else:
        log+="There were no updated (new and modified) server Folders since the last sync.\n" 

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

    #client_updated_folders: new and modified
    client_updated_folders = local_session.query(Folder).filter(and_(
      Folder.modified > last_client_sync, Folder.deleted==False)).all()

    if client_updated_folders:
        nn+=len(client_updated_folders)
        log+=f"Updated (new and modified) client Folders since the last sync: {len(client_updated_folders)}.\n"
    else:
        log+="There were no updated (new and modified) client Folders since the last sync.\n" 

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
            context = Context()
            local_session.add(context)
            # next line important: local db task.tid is the unique key that 
            # links to the foreign key in context and folder tables
            context.tid = sc.id
        else:
            action = "updated"
            
        # Note that the foreign key that the server uses task.context_tid points to context.id is different
        # between postgres and sqlite postgres points to context.id and local
        # sqlite db points to context.tid but the actual values are identical
        context.title = sc.title
        context.default = sc.default
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
            context = p.Context(1000, "temp") # needs a title but will be changed below
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
        context.default = cc.default
        context.textcolor = cc.textcolor

        remote_session.commit()

        log += f"{action} - id: {context.id} title: {context.title} \n"

    # updated server folders -> client
    if server_updated_folders:
        log += "\nContexts that were updated/created on the Server that need to be updated/created on the Client:\n"
    for sf in server_updated_folders:

        folder = local_session.query(Folder).filter_by(tid=sf.id).first()

        if not folder:
            action = "created"
            folder = Folder()
            local_session.add(folder)
            # next line important: local db task.tid is the unique key that 
            # links to the foreign key in context and folder tables
            folder.tid = sf.id
        else:
            action = "updated"
            
        # Note that the foreign key that the server uses task.context_tid points to context.id is different
        # between postgres and sqlite postgres points to context.id and local
        # sqlite db points to context.tid but the actual values are identical
        folder.title = sf.title
        #folder.default = sf.default
        folder.textcolor = sf.textcolor

        local_session.commit() #new/updated client folder commit
        
        log += f"{action} - id: {folder.id} tid: {folder.tid} title: {folder.title} \n"
        
    # updated client folders -> server
    if client_updated_folders:
        log += "\nContexts that were updated/created on Client that need to be updated/created on Server:\n"
    for cf in client_updated_folders:

        folder = remote_session.query(p.Folder).filter_by(id=cf.tid).first()

        if not folder:
            action = "created"
            folder = p.Folder(1000, "temp") # needs a title but will be changed below
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
        #folder.default = cf.default
        folder.textcolor = cf.textcolor

        remote_session.commit()

        log += f"{action} - id: {folder.id} title: {folder.title} \n"

    # the following is intended to catch contexts deleted on the server
    if 0:
        server_context_tids = set([sc.id for sc in remote_session.query(p.Context)])
        client_context_tids = set([cc.tid for cc in local_session.query(Context)])

        client_not_server = client_context_tids - server_context_tids

        for tid in client_not_server:
            cc = local_session.query(Context).filter_by(tid=tid).one()

            tasks = local_session.query(Task).filter_by(context_tid=tid).all()
            for t in tasks:
                t.context_tid = 1
                log+="client task id: {t.id} title {t.title} was put in context 'No Context'\n"

            title = cc.title
            local_session.delete(cc)
            local_session.commit()
            #Note that the delete sets context_tid=None for all tasks in the context
            #I wonder how you set to zero - this is done "manually" below
            log+= "Deleted client context tid: {cc.tid}  - title {cc.title}"

            #These tasks are marked as changed by server so don't need to do this
            #They temporarily pick of context_tid of None after the context is deleted on the client
            #When tasks are updated later in sync they pick up correct folder_tid=0

        #no code for client deleted contexts yet
        
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
        # between postgres and sqlite postgres points to context.id and local
        # sqlite db points to context.tid but the actual values are identical
        task.context_tid = st.context_tid
        task.duedate = st.duedate
        task.duetime = st.duetime if st.duetime else None #########
        task.remind = st.remind
        task.startdate = st.startdate if st.startdate else st.added ################ may 2, 2012
        task.folder_tid = st.folder_tid 
        task.title = st.title if st.title else '' # somehow st.title can be None and needs to be a string
        task.added = st.added
        task.star = st.star
        task.priority = st.priority
        task.tag = st.tag
        task.completed = st.completed if st.completed else None
        task.note = st.note
        #task.modified = st.modified #not needed because sqlalchemy inserts default according lmdb_s.py

        local_session.commit() #new/updated client task commit

        log += f"{action} - id: {task.id} tid: {task.tid}; star: {task.star}; priority: {task.priority}; completed: {task.completed}; title: {task.title[:32]}\n"

        if task.tag:
            for tk in task.taskkeywords:
                local_session.delete(tk)
            local_session.commit()

            for kwn in task.tag.split(','):
                keyword = local_session.query(Keyword).filter_by(name=kwn).first()
                if keyword is None:
                    keyword = Keyword(kwn)
                    local_session.add(keyword)
                    local_session.commit()
                tk = TaskKeyword(task,keyword)
                local_session.add(tk)

            local_session.commit()
            
        tasklist.append(task)
        
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
        task.startdate = ct.startdate if ct.startdate else ct.added ################ may 2, 2012
        task.folder_tid = ct.folder_tid 
        task.title = ct.title
        task.added = ct.added
        task.star = ct.star
        task.priority = ct.priority
        task.tag = ct.tag
        task.completed = ct.completed if ct.completed else None
        task.note = ct.note
        #task.modified = ct.modified # not necessary - done by sqlite

        remote_session.commit() #new/updated client task commit

        log += f"{action} - id: {task.id}; star: {task.star}; priority: {task.priority}; completed: {task.completed}; title: {task.title[:32]}\n"

        if task.tag:
            for tk in task.taskkeywords:
                remote_session.delete(tk)
            remote_session.commit()

            for kwn in task.tag.split(','):
                keyword = remote_session.query(p.Keyword).filter_by(name=kwn).first()
                if keyword is None:
                    keyword = p.Keyword(kwn)
                    remote_session.add(keyword)
                    remote_session.commit()
                tk = p.TaskKeyword(task,keyword)
                remote_session.add(tk)

            remote_session.commit()
            
        # probably need two tasklists - server and client for whoosh updating
        #tasklist.append(task) #really need to look at this 09092015
        
    # Delete from client tasks deleted on server
    # uses deletelist
    for t in server_deleted_tasks:
        task = local_session.query(Task).filter_by(tid=t.id).first()
        if task:
                    
            log+="Task deleted on Server deleted task on Client - id: {id_}; tid: {tid}; title: {title}\n".format(id_=task.id,tid=task.tid,title=task.title[:32])
            
            deletelist.append(task.id)
            
            for tk in task.taskkeywords:
                local_session.delete(tk)
            local_session.commit()
        
            local_session.delete(task)
            local_session.commit() 
            
        else:
            
            log+="Task deleted on Server unsuccessful trying to delete on Client - could not find Client Task with tid = {0}\n".format(t.id)   

    # uses deletelist 
    tids_to_delete = []
    client_tasks = []
    for t in client_deleted_tasks:
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
            #tids_to_delete.append(t.tid)
            #client_tasks.append(t)
        #else:
        deletelist.append(t.id)
        for tk in t.taskkeywords:
            local_session.delete(tk)
        local_session.commit()
                
        local_session.delete(t)
        local_session.commit()     
         
    client_sync.timestamp = datetime.datetime.now() + datetime.timedelta(seconds=5) # giving a little buffer if the db takes time to update on client or server

    # saw definitively that the resulting timestamp could be earlier than when the server tasks were modified -- no idea why
    #connection = p.remote_engine.connect()
    #result = connection.execute("select extract(epoch from now())")
    #server_sync.timestamp = datetime.datetime.fromtimestamp(result.scalar()) + datetime.timedelta(seconds=10) # not sure why this is necessary but it really is

    task = remote_session.query(p.Task).get(1) #Call Spectacles ...
    priority = task.priority
    task.priority = priority + 1 if priority < 3 else 0
    remote_session.commit()
    server_sync.timestamp = task.modified + datetime.timedelta(seconds=5) # not sure why this is necessary but it really is

    local_session.commit()  

    log+="New Sync times\n"
    log+="client timestamp: {}\n".format(client_sync.timestamp.isoformat(' '))
    log+="server timestamp: {}\n".format(server_sync.timestamp.isoformat(' '))
    log+= "Time is {0}\n\n".format(datetime.datetime.now())

    log+=("\n\n***************** END SYNC *******************************************")

    #return log,changes,tasklist,deletelist 
    with open('log', 'w') as f:
        f.write(log)

    return nn

if __name__ == "__main__":
    nn = synchronize(True)
    print(f"Number of items changed = {nn}")
