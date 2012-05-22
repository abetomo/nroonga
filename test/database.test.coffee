nroonga = require('../lib/nroonga')
fs = require('fs')
should = require('should')

temporaryDatabase = (callback) ->
  tempdir = 'test/tmp'
  fs.mkdir tempdir, ->
    databaseName = "tempdb-#{process.pid}-#{(new Date()).valueOf()}"
    db = new nroonga.Database(tempdir + '/' + databaseName)

    try
      callback(db)
    finally
      fs.readdir tempdir, (err, files) ->
        throw err if err?
        re = RegExp('^' + databaseName)
        for file in files when file.match(re)
          fs.unlink(tempdir + '/' + file)

withTestDatabase = (callback) ->
  temporaryDatabase (db) ->
    db.commandSync 'table_create',
      name: 'Site'
      flags: 'TABLE_HASH_KEY'
      key_type: 'ShortText'
    db.commandSync 'column_create',
      table: 'Site'
      name: 'title'
      flags: 'COLUMN_SCALAR'
      type: 'ShortText'

    db.commandSync 'table_create',
      name: 'Terms'
      flags: 'TABLE_PAT_KEY|KEY_NORMALIZE'
      key_type: 'ShortText'
      default_tokenizer: 'TokenBigram'
    db.commandSync 'column_create',
      table: 'Terms'
      name: 'entry_title'
      flags: 'COLUMN_INDEX|WITH_POSITION'
      type: 'Site'
      source: 'title'

    db.commandSync 'load',
      table: 'Site'
      values: JSON.stringify [
        _key: "http://groonga.org/"
        title: "groonga - An open-source fulltext search engine and column store"
      ,
        _key: "http://groonga.rubyforge.org/"
        title: "Fulltext search by Ruby with groonga - Ranguba"
      ,
        _key: "http://mroonga.github.com/"
        title: "Groonga storage engine - Fast fulltext search on MySQL"
      ]

    callback(db)

describe 'nroonga.Database', ->
  db = new nroonga.Database()

  describe '#commandSync', ->
    status = db.commandSync('status')
    it 'should return groonga results', ->
      should.exist(status.version)

  describe '#command', ->
    it 'should return groonga results', (done) ->
      db.command 'status', (error, data) ->
        throw error if error
        should.exist(data.version)
        done()

describe 'database whose name is not string', ->
  it 'should throw an exception', ->
    (->
      new nroonga.Database(1)
    ).should.throw()

describe 'database with data stored', ->
  it 'should select records', (done) ->
    withTestDatabase (db) ->
      matched = db.commandSync('select', table: 'Site')
      matched[0][0][0].should.equal(3)
      done()

  it 'should search by query', (done) ->
    withTestDatabase (db) ->
      matched = db.commandSync 'select',
        table: 'Site'
        match_columns: 'title'
        query: 'ruby'
      matched[0][0][0].should.equal(1)
      done()

  it 'should search by query including space', (done) ->
    withTestDatabase (db) ->
      matched = db.commandSync 'select',
        table: 'Site'
        match_columns: 'title'
        query: 'search ranguba'
      matched[0][0][0].should.equal(1)
      done()
