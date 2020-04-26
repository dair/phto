#!/bin/bash
# это надо запускать из-под суперпользователя базы данных. В debian это пользователь postgres

DB_USER=phto
DB_PASSWD=phto_gfhjkm

psql -c "create user $DB_USER with login password '$DB_PASSWD';"

