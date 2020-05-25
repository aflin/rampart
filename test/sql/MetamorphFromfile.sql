-- Test metamorph
-- 
-- Set defaults for known state, eg. in case texis.cnf mods:
set querysettings = 'defaults';
-- Turn off query protection so we can do linear searches etc.:
set querysettings = 'protectionoff';


create table test (x varchar(100), y varchar(100));

insert into test values ('alien', 'alien');
update test set y=fromfiletext('text/alien') where y = 'alien';
insert into test values ('constn', 'constn');
update test set y=fromfiletext('text/constn') where y = 'constn';
insert into test values ('declare', 'declare');
update test set y=fromfiletext('text/declare') where y = 'declare';
insert into test values ('events', 'events');
update test set y=fromfiletext('text/events') where y = 'events';
insert into test values ('garden', 'garden');
update test set y=fromfiletext('text/garden') where y = 'garden';
insert into test values ('gettysbg.txt', 'gettysbg.txt');
update test set y=fromfiletext('text/gettysbg.txt') where y = 'gettysbg.txt';
insert into test values ('kids', 'kids');
update test set y=fromfiletext('text/kids') where y = 'kids';
insert into test values ('liberty', 'liberty');
update test set y=fromfiletext('text/liberty') where y = 'liberty';
insert into test values ('qadhafi', 'qadhafi');
update test set y=fromfiletext('text/qadhafi') where y = 'qadhafi';
insert into test values ('socrates', 'socrates');
update test set y=fromfiletext('text/socrates') where y = 'socrates';

select x from test where y like '~power ~struggle';
create metamorph index ix_mm on test (y) ;
select x from test where y like '~power ~struggle';

drop table test;

