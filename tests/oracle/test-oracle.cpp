//
// // Copyright (C) 2004-2007 Maciej Sobczak, Stephen Hutton
// Distributed under the Boost Software License, Version 1.0.
// (See accompanying file LICENSE_1_0.txt or copy at
// http://www.boost.org/LICENSE_1_0.txt)
//

#include "soci/soci.h"
#include "soci/oracle/soci-oracle.h"
#include "common-tests.h"
#include <iostream>
#include <string>
#include <cstring>
#include <ctime>

using namespace soci;
using namespace soci::tests;

std::string connectString;
backend_factory const &backEnd = *soci::factory_oracle();

// Extra tests for date/time
TEST_CASE("Oracle datetime", "[oracle][datetime]")
{
    session sql(backEnd, connectString);

    {
        std::time_t now = std::time(NULL);
        std::tm t1, t2;
        t2 = *std::localtime(&now);

        sql << "select t from (select :t as t from dual)",
            into(t1), use(t2);

        CHECK(t1.tm_sec == t2.tm_sec);
        CHECK(t1.tm_min == t2.tm_min);
        CHECK(t1.tm_hour == t2.tm_hour);
        CHECK(t1.tm_mday == t2.tm_mday);
        CHECK(t1.tm_mon == t2.tm_mon);
        CHECK(t1.tm_year == t2.tm_year);
        CHECK(t1.tm_wday == t2.tm_wday);
        CHECK(t1.tm_yday == t2.tm_yday);
        CHECK(t1.tm_isdst == t2.tm_isdst);

        // make sure the date is stored properly in Oracle
        char buf[25];
        strftime(buf, sizeof(buf), "%m-%d-%Y %H:%M:%S", &t2);

        std::string t_out;
        std::string format("MM-DD-YYYY HH24:MI:SS");
        sql << "select to_char(t, :format) from (select :t as t from dual)",
            into(t_out), use(format), use(t2);

        CHECK(t_out == std::string(buf));
    }

    {
        // date and time - before year 2000
        std::time_t then = std::time(NULL) - 17*365*24*60*60;
        std::tm t1, t2;
        t2 = *std::localtime(&then);

        sql << "select t from (select :t as t from dual)",
             into(t1), use(t2);

        CHECK(t1.tm_sec == t2.tm_sec);
        CHECK(t1.tm_min == t2.tm_min);
        CHECK(t1.tm_hour == t2.tm_hour);
        CHECK(t1.tm_mday == t2.tm_mday);
        CHECK(t1.tm_mon == t2.tm_mon);
        CHECK(t1.tm_year == t2.tm_year);
        CHECK(t1.tm_wday == t2.tm_wday);
        CHECK(t1.tm_yday == t2.tm_yday);
        CHECK(t1.tm_isdst == t2.tm_isdst);

        // make sure the date is stored properly in Oracle
        char buf[25];
        strftime(buf, sizeof(buf), "%m-%d-%Y %H:%M:%S", &t2);

        std::string t_out;
        std::string format("MM-DD-YYYY HH24:MI:SS");
        sql << "select to_char(t, :format) from (select :t as t from dual)",
            into(t_out), use(format), use(t2);

        CHECK(t_out == std::string(buf));
    }
}

// explicit calls test
TEST_CASE("Oracle explicit calls", "[oracle]")
{
    session sql(backEnd, connectString);

    statement st(sql);
    st.alloc();
    int i = 0;
    st.exchange(into(i));
    st.prepare("select 7 from dual");
    st.define_and_bind();
    st.execute(1);
    CHECK(i == 7);
}

// DDL + blob test

struct blob_table_creator : public table_creator_base
{
    blob_table_creator(session & sql)
    : table_creator_base(sql)
    {
        sql <<
            "create table soci_test ("
            "    id number(10) not null,"
            "    img blob"
            ")";
    }
};

TEST_CASE("Oracle blob", "[oracle][blob]")
{
    session sql(backEnd, connectString);

    blob_table_creator tableCreator(sql);

    char buf[] = "abcdefghijklmnopqrstuvwxyz";
    sql << "insert into soci_test (id, img) values (7, empty_blob())";

    {
        blob b(sql);

        oracle_session_backend *sessionBackEnd
            = static_cast<oracle_session_backend *>(sql.get_backend());

        oracle_blob_backend *blobBackEnd
            = static_cast<oracle_blob_backend *>(b.get_backend());

        OCILobDisableBuffering(sessionBackEnd->svchp_,
            sessionBackEnd->errhp_, blobBackEnd->lobp_);

        sql << "select img from soci_test where id = 7", into(b);
        CHECK(b.get_len() == 0);

        // note: blob offsets start from 1
        b.write(1, buf, sizeof(buf));
        CHECK(b.get_len() == sizeof(buf));
        b.trim(10);
        CHECK(b.get_len() == 10);

        // append does not work (Oracle bug #886191 ?)
        //b.append(buf, sizeof(buf));
        //CHECK(b.get_len() == sizeof(buf) + 10);
        sql.commit();
    }

    {
        blob b(sql);
        sql << "select img from soci_test where id = 7", into(b);
        //CHECK(b.get_len() == sizeof(buf) + 10);
        CHECK(b.get_len() == 10);
        char buf2[100];
        b.read(1, buf2, 10);
        CHECK(strncmp(buf2, "abcdefghij", 10) == 0);
    }
}

// nested statement test
// (the same syntax is used for output cursors in PL/SQL)

struct basic_table_creator : public table_creator_base
{
    basic_table_creator(session & sql)
        : table_creator_base(sql)
    {
        sql <<
                    "create table soci_test ("
                    "    id number(5) not null,"
                    "    name varchar2(100),"
                    "    code number(5)"
                    ")";
    }
};

TEST_CASE("Oracle nested statement", "[oracle][blob]")
{
    session sql(backEnd, connectString);
    basic_table_creator tableCreator(sql);

    int id;
    std::string name;
    {
        statement st1 = (sql.prepare <<
            "insert into soci_test (id, name) values (:id, :name)",
            use(id), use(name));

        id = 1; name = "John"; st1.execute(1);
        id = 2; name = "Anna"; st1.execute(1);
        id = 3; name = "Mike"; st1.execute(1);
    }

    statement stInner(sql);
    statement stOuter = (sql.prepare <<
        "select cursor(select name from soci_test order by id)"
        " from soci_test where id = 1",
        into(stInner));
    stInner.exchange(into(name));
    stOuter.execute();
    stOuter.fetch();

    std::vector<std::string> names;
    while (stInner.fetch())    { names.push_back(name); }

    REQUIRE(names.size() == 3);
    CHECK(names[0] == "John");
    CHECK(names[1] == "Anna");
    CHECK(names[2] == "Mike");
}


// ROWID test
TEST_CASE("Oracle rowid", "[oracle][rowid]")
{
    session sql(backEnd, connectString);
    basic_table_creator tableCreator(sql);

    sql << "insert into soci_test(id, name) values(7, \'John\')";

    rowid rid(sql);
    sql << "select rowid from soci_test where id = 7", into(rid);

    int id;
    std::string name;
    sql << "select id, name from soci_test where rowid = :rid",
        into(id), into(name), use(rid);

    CHECK(id == 7);
    CHECK(name == "John");
}

// Stored procedures
struct procedure_creator : procedure_creator_base
{
    procedure_creator(session & sql)
        : procedure_creator_base(sql)
    {
        sql <<
             "create or replace procedure soci_test(output out varchar2,"
             "input in varchar2) as "
             "begin output := input; end;";
    }
};

TEST_CASE("Oracle stored procedure", "[oracle][stored-procedure]")
{
    session sql(backEnd, connectString);
    procedure_creator procedure_creator(sql);

    std::string in("my message");
    std::string out;
    statement st = (sql.prepare <<
        "begin soci_test(:output, :input); end;",
        use(out, "output"),
        use(in, "input"));
    st.execute(1);
    CHECK(out == in);

    // explicit procedure syntax
    {
        std::string in("my message2");
        std::string out;
        procedure proc = (sql.prepare <<
            "soci_test(:output, :input)",
            use(out, "output"), use(in, "input"));
        proc.execute(1);
        CHECK(out == in);
    }
}

// bind into user-defined objects
struct string_holder
{
    string_holder() {}
    string_holder(const char* s) : s_(s) {}
    string_holder(std::string s) : s_(s) {}
    std::string get() const { return s_; }
private:
    std::string s_;
};

namespace soci
{
    template <>
    struct type_conversion<string_holder>
    {
        typedef std::string base_type;
        static void from_base(const std::string &s, indicator /* ind */,
            string_holder &sh)
        {
            sh = string_holder(s);
        }

        static void to_base(const string_holder &sh, std::string &s, indicator &ind)
        {
            s = sh.get();
            ind = i_ok;
        }
    };
}

struct in_out_procedure_creator : public procedure_creator_base
{
    in_out_procedure_creator(session & sql)
        : procedure_creator_base(sql)
    {
        sql << "create or replace procedure soci_test(s in out varchar2)"
                " as begin s := s || s; end;";
    }
};

struct returns_null_procedure_creator : public procedure_creator_base
{
    returns_null_procedure_creator(session & sql)
        : procedure_creator_base(sql)
    {
        sql << "create or replace procedure soci_test(s in out varchar2)"
            " as begin s := NULL; end;";
    }
};

TEST_CASE("Oracle user-defined objects", "[oracle][type_conversion]")
{
    session sql(backEnd, connectString);
    {
        basic_table_creator tableCreator(sql);

        int id(1);
        string_holder in("my string");
        sql << "insert into soci_test(id, name) values(:id, :name)", use(id), use(in);

        string_holder out;
        sql << "select name from soci_test", into(out);
        CHECK(out.get() == "my string");

        row r;
        sql << "select * from soci_test", into(r);
        string_holder dynamicOut = r.get<string_holder>(1);
        CHECK(dynamicOut.get() == "my string");
    }
}

TEST_CASE("Oracle user-defined objects in/out", "[oracle][type_conversion]")
{
    session sql(backEnd, connectString);

    // test procedure with user-defined type as in-out parameter
    {
        in_out_procedure_creator procedureCreator(sql);

        std::string sh("test");
        procedure proc = (sql.prepare << "soci_test(:s)", use(sh));
        proc.execute(1);
        CHECK(sh == "testtest");
    }

    // test procedure with user-defined type as in-out parameter
    {
        in_out_procedure_creator procedureCreator(sql);

        string_holder sh("test");
        procedure proc = (sql.prepare << "soci_test(:s)", use(sh));
        proc.execute(1);
        CHECK(sh.get() == "testtest");
    }
}

TEST_CASE("Oracle null user-defined objects in/out", "[oracle][null][type_conversion]")
{
    session sql(backEnd, connectString);

    // test procedure which returns null
    returns_null_procedure_creator procedureCreator(sql);

    string_holder sh;
    indicator ind = i_ok;
    procedure proc = (sql.prepare << "soci_test(:s)", use(sh, ind));
    proc.execute(1);
    CHECK(ind == i_null);
}

// test bulk insert features
TEST_CASE("Oracle bulk insert", "[oracle][insert][bulk]")
{
    session sql(backEnd, connectString);

    basic_table_creator tableCreator(sql);

    // verify exception is thrown if vectors of unequal size are passed in
    {
        std::vector<int> ids;
        ids.push_back(1);
        ids.push_back(2);
        std::vector<int> codes;
        codes.push_back(1);
        std::string error;

        try
        {
            sql << "insert into soci_test(id,code) values(:id,:code)",
                use(ids), use(codes);
        }
        catch (soci_error const &e)
        {
            error = e.what();
        }
        CAPTURE(error);
        CHECK(error.find("Bind variable size mismatch")
            != std::string::npos);

        try
        {
            sql << "select from soci_test", into(ids), into(codes);
        }
        catch (std::exception const &e)
        {
            error = e.what();
        }
        CAPTURE(error);
        CHECK(error.find("Bind variable size mismatch")
            != std::string::npos);
    }

    // verify partial insert occurs when one of the records is bad
    {
        std::vector<int> ids;
        ids.push_back(100);
        ids.push_back(1000000); // too big for column

        std::string error;
        try
        {
            sql << "insert into soci_test (id) values(:id)", use(ids, "id");
        }
        catch (soci_error const &e)
        {
            error = e.what();
            //TODO e could be made to tell which row(s) failed
        }
        sql.commit();
        CAPTURE(error);
        CHECK(error.find("ORA-01438") != std::string::npos);
        int count(7);
        sql << "select count(*) from soci_test", into(count);
        CHECK(count == 1);
        sql << "delete from soci_test";
    }

    // test insert
    {
        std::vector<int> ids;
        for (int i = 0; i != 3; ++i)
        {
            ids.push_back(i+10);
        }

        statement st = (sql.prepare << "insert into soci_test(id) values(:id)",
                            use(ids));
        st.execute(1);
        int count;
        sql << "select count(*) from soci_test", into(count);
        CHECK(count == 3);
    }

    //verify an exception is thrown if into vector is zero length
    {
        std::vector<int> ids;
        CHECK_THROWS_AS((sql << "select id from soci_test", into(ids)), soci_error);
    }

    // verify an exception is thrown if use vector is zero length
    {
        std::vector<int> ids;
        CHECK_THROWS_AS((sql << "insert into soci_test(id) values(:id)", use(ids)), soci_error);
    }

    // test "no data" condition
    {
        std::vector<indicator> inds(3);
        std::vector<int> ids_out(3);
        statement st = (sql.prepare << "select id from soci_test where 1=0",
                        into(ids_out, inds));

        // false return value means "no data"
        CHECK(st.execute(1) == false);

        // that's it - nothing else is guaranteed
        // and nothing else is to be tested here
    }

    // test NULL indicators
    {
        std::vector<int> ids(3);
        sql << "select id from soci_test", into(ids);

        std::vector<indicator> inds_in;
        inds_in.push_back(i_ok);
        inds_in.push_back(i_null);
        inds_in.push_back(i_ok);

        std::vector<int> new_codes;
        new_codes.push_back(10);
        new_codes.push_back(11);
        new_codes.push_back(10);

        sql << "update soci_test set code = :code where id = :id",
                use(new_codes, inds_in), use(ids);

        std::vector<indicator> inds_out(3);
        std::vector<int> codes(3);

        sql << "select code from soci_test", into(codes, inds_out);
        REQUIRE(codes.size() == 3);
        REQUIRE(inds_out.size() == 3);
        CHECK(codes[0] == 10);
        CHECK(codes[2] == 10);
        CHECK(inds_out[0] == i_ok);
        CHECK(inds_out[1] == i_null);
        CHECK(inds_out[2] == i_ok);
    }

    // verify an exception is thrown if null is selected
    //  and no indicator was provided
    {
        std::string msg;
        std::vector<int> intos(3);
        try
        {
            sql << "select code from soci_test", into(intos);
        }
        catch (soci_error const &e)
        {
            msg = e.what();
        }
        CHECK(msg == "Null value fetched and no indicator defined." );
    }

    // test basic select
    {
        const size_t sz = 3;
        std::vector<indicator> inds(sz);
        std::vector<int> ids_out(sz);
        statement st = (sql.prepare << "select id from soci_test",
            into(ids_out, inds));
        const bool gotData = st.execute(true);
        CHECK(gotData);
        REQUIRE(ids_out.size() == sz);
        CHECK(ids_out[0] == 10);
        CHECK(ids_out[2] == 12);
        REQUIRE(inds.size() == 3);
        CHECK(inds[0] == i_ok);
        CHECK(inds[1] == i_ok);
        CHECK(inds[2] == i_ok);
    }

    // verify execute(0)
    {
        std::vector<int> ids_out(2);
        statement st = (sql.prepare << "select id from soci_test",
            into(ids_out));

        st.execute();
        REQUIRE(ids_out.size() == 2);
        bool gotData = st.fetch();
        CHECK(gotData);
        REQUIRE(ids_out.size() == 2);
        CHECK(ids_out[0] == 10);
        CHECK(ids_out[1] == 11);
        gotData = st.fetch();
        CHECK(gotData);
        REQUIRE(ids_out.size() == 1);
        CHECK(ids_out[0] == 12);
        gotData = st.fetch();
        CHECK(gotData == false);
    }

    // verify resizing happens if vector is larger
    // than number of rows returned
    {
        std::vector<int> ids_out(4); // one too many
        statement st2 = (sql.prepare << "select id from soci_test",
            into(ids_out));
        bool gotData = st2.execute(true);
        CHECK(gotData);
        REQUIRE(ids_out.size() == 3);
        CHECK(ids_out[0] == 10);
        CHECK(ids_out[2] == 12);
    }

    // verify resizing happens properly during fetch()
    {
        std::vector<int> more;
        more.push_back(13);
        more.push_back(14);
        sql << "insert into soci_test(id) values(:id)", use(more);

        std::vector<int> ids(2);
        statement st3 = (sql.prepare << "select id from soci_test", into(ids));
        bool gotData = st3.execute(true);
        CHECK(gotData);
        CHECK(ids[0] == 10);
        CHECK(ids[1] == 11);

        gotData = st3.fetch();
        CHECK(gotData);
        CHECK(ids[0] == 12);
        CHECK(ids[1] == 13);

        gotData = st3.fetch();
        CHECK(gotData);
        REQUIRE(ids.size() == 1);
        CHECK(ids[0] == 14);

        gotData = st3.fetch();
        CHECK(gotData == false);
    }
}

// more tests for bulk fetch
TEST_CASE("Oracle bulk insert", "[oracle][fetch][bulk]")
{
    session sql(backEnd, connectString);

    basic_table_creator tableCreator(sql);

    std::vector<int> in;
    for (int i = 1; i <= 10; ++i)
    {
        in.push_back(i);
    }

    sql << "insert into soci_test (id) values(:id)", use(in);

    int count(0);
    sql << "select count(*) from soci_test", into(count);
    CHECK(count == 10);

    // verify that the exception is thrown when trying to resize
    // the output vector to the size that is bigger than that
    // at the time of binding
    {
        std::vector<int> out(4);
        statement st = (sql.prepare <<
            "select id from soci_test", into(out));

        st.execute();

        st.fetch();
        REQUIRE(out.size() == 4);
        CHECK(out[0] == 1);
        CHECK(out[1] == 2);
        CHECK(out[2] == 3);
        CHECK(out[3] == 4);
        out.resize(5); // this should be detected as error
        try
        {
            st.fetch();
            FAIL("expected exception not thrown");
        }
        catch (soci_error const &e)
        {
            CHECK(std::string(e.what()) ==
                "Increasing the size of the output vector is not supported.");
        }
    }

    // on the other hand, downsizing is OK
    {
        std::vector<int> out(4);
        statement st = (sql.prepare <<
            "select id from soci_test", into(out));

        st.execute();

        st.fetch();
        REQUIRE(out.size() == 4);
        CHECK(out[0] == 1);
        CHECK(out[1] == 2);
        CHECK(out[2] == 3);
        CHECK(out[3] == 4);
        out.resize(3); // ok
        st.fetch();
        REQUIRE(out.size() == 3);
        CHECK(out[0] == 5);
        CHECK(out[1] == 6);
        CHECK(out[2] == 7);
        out.resize(4); // ok, not bigger than initially
        st.fetch();
        REQUIRE(out.size() == 3); // downsized because of end of data
        CHECK(out[0] == 8);
        CHECK(out[1] == 9);
        CHECK(out[2] == 10);
        bool gotData = st.fetch();
        CHECK(gotData == false); // end of data
    }
}

struct person
{
    int id;
    std::string firstName;
    string_holder lastName; //test mapping of type_conversion-based types
    std::string gender;
};

// Object-Relational Mapping
// Note: Use the values class as shown below in type_conversions
// to achieve object relational mapping.  The values class should
// not be used directly in any other fashion.
namespace soci
{
    // name-based conversion
    template<> struct type_conversion<person>
    {
        typedef values base_type;

        static void from_base(values const &v, indicator /* ind */, person &p)
        {
            // ignoring possibility that the whole object might be NULL

            p.id = v.get<int>("ID");
            p.firstName = v.get<std::string>("FIRST_NAME");
            p.lastName = v.get<string_holder>("LAST_NAME");
            p.gender = v.get<std::string>("GENDER", "unknown");
        }

        static void to_base(person const & p, values & v, indicator & ind)
        {
            v.set("ID", p.id);
            v.set("FIRST_NAME", p.firstName);
            v.set("LAST_NAME", p.lastName);
            v.set("GENDER", p.gender, p.gender.empty() ? i_null : i_ok);
            ind = i_ok;
        }
    };
}

struct person_table_creator : public table_creator_base
{
    person_table_creator(session & sql)
        : table_creator_base(sql)
    {
        sql << "create table soci_test(id numeric(5,0) NOT NULL,"
             << " last_name varchar2(20), first_name varchar2(20), "
                " gender varchar2(10))";
    }
};

struct times100_procedure_creator : public procedure_creator_base
{
    times100_procedure_creator(session & sql)
        : procedure_creator_base(sql)
    {
        sql << "create or replace procedure soci_test(id in out number)"
               " as begin id := id * 100; end;";
    }
};

TEST_CASE("Oracle ORM", "[oracle][orm]")
{
    session sql(backEnd, connectString);

    {
        person_table_creator tableCreator(sql);

        person p;
        p.id = 1;
        p.lastName = "Smith";
        p.firstName = "Pat";
        sql << "insert into soci_test(id, first_name, last_name, gender) "
            << "values(:ID, :FIRST_NAME, :LAST_NAME, :GENDER)", use(p);

        // p should be unchanged
        CHECK(p.id == 1);
        CHECK(p.firstName == "Pat");
        CHECK(p.lastName.get() == "Smith");

        person p1;
        sql << "select * from soci_test", into(p1);
        CHECK(p1.id == 1);
        CHECK(p1.firstName == "Pat");
        CHECK(p1.lastName.get() == "Smith");
        CHECK(p1.gender == "unknown");

        p.firstName = "Patricia";
        sql << "update soci_test set first_name = :FIRST_NAME "
               "where id = :ID", use(p);

        // p should be unchanged
        CHECK(p.id == 1);
        CHECK(p.firstName == "Patricia");
        CHECK(p.lastName.get() == "Smith");
        // Note: gender is now "unknown" because of the mapping, not ""
        CHECK(p.gender == "unknown");

        person p2;
        sql << "select * from soci_test", into(p2);
        CHECK(p2.id == 1);
        CHECK(p2.firstName == "Patricia");
        CHECK(p2.lastName.get() == "Smith");

        // insert a second row so we can test fetching
        person p3;
        p3.id = 2;
        p3.firstName = "Joe";
        p3.lastName = "Smith";
        sql << "insert into soci_test(id, first_name, last_name, gender) "
            << "values(:ID, :FIRST_NAME, :LAST_NAME, :GENDER)", use(p3);

        person p4;
        statement st = (sql.prepare << "select * from soci_test order by id",
                    into(p4));

        st.execute();
        bool gotData = st.fetch();
        CHECK(gotData);
        CHECK(p4.id == 1);
        CHECK(p4.firstName == "Patricia");

        gotData = st.fetch();
        CHECK(gotData);
        CHECK(p4.id == 2);
        CHECK(p4.firstName == "Joe");
        gotData = st.fetch();
        CHECK(gotData == false);
    }

    // test with stored procedure
    {
        times100_procedure_creator procedureCreator(sql);

        person p;
        p.id = 1;
        p.firstName = "Pat";
        p.lastName = "Smith";
        procedure proc = (sql.prepare << "soci_test(:ID)", use(p));
        proc.execute(1);
        CHECK(p.id == 100);
        CHECK(p.firstName == "Pat");
        CHECK(p.lastName.get() == "Smith");
    }

    // test with stored procedure which returns null
    {
        returns_null_procedure_creator procedureCreator(sql);

        std::string msg;
        person p;
        try
        {
            procedure proc = (sql.prepare << "soci_test(:FIRST_NAME)",
                                use(p));
            proc.execute(1);
        }
        catch (soci_error& e)
        {
            msg = e.what();
        }
        CHECK(msg == "Null value not allowed for this type");

        procedure proc = (sql.prepare << "soci_test(:GENDER)",
                                use(p));
        proc.execute(1);
        CHECK(p.gender == "unknown");

    }
}

// Experimental support for position based O/R Mapping

// additional type for position-based test
struct person2
{
    int id;
    std::string firstName;
    std::string lastName;
    std::string gender;
};

// additional type for stream-like test
struct person3 : person2 {};

namespace soci
{
    // position-based conversion
    template<> struct type_conversion<person2>
    {
        typedef values base_type;

        static void from_base(values const &v, indicator /* ind */, person2 &p)
        {
            p.id = v.get<int>(0);
            p.firstName = v.get<std::string>(1);
            p.lastName = v.get<std::string>(2);
            p.gender = v.get<std::string>(3, "whoknows");
        }

        // What about the "to" part? Does it make any sense to have it?
    };

    // stream-like conversion
    template<> struct type_conversion<person3>
    {
        typedef values base_type;

        static void from_base(values const &v, indicator /* ind */, person3 &p)
        {
            v >> p.id >> p.firstName >> p.lastName >> p.gender;
        }
        // TODO: The "to" part is certainly needed.
    };
}

TEST_CASE("Oracle ORM by index", "[oracle][orm]")
{
    session sql(backEnd, connectString);

    person_table_creator tableCreator(sql);

    person p;
    p.id = 1;
    p.lastName = "Smith";
    p.firstName = "Patricia";
    sql << "insert into soci_test(id, first_name, last_name, gender) "
        << "values(:ID, :FIRST_NAME, :LAST_NAME, :GENDER)", use(p);

    //  test position-based conversion
    person2 p3;
    sql << "select id, first_name, last_name, gender from soci_test", into(p3);
    CHECK(p3.id == 1);
    CHECK(p3.firstName == "Patricia");
    CHECK(p3.lastName == "Smith");
    CHECK(p3.gender == "whoknows");

    sql << "update soci_test set gender = 'F' where id = 1";

    // additional test for stream-like conversion
    person3 p4;
    sql << "select id, first_name, last_name, gender from soci_test", into(p4);
    CHECK(p4.id == 1);
    CHECK(p4.firstName == "Patricia");
    CHECK(p4.lastName == "Smith");
    CHECK(p4.gender == "F");
}

//
// Backwards compatibility - support use of large strings with
// columns of type LONG
///
struct long_table_creator : public table_creator_base
{
    long_table_creator(session & sql)
        : table_creator_base(sql)
    {
        sql << "create table soci_test(l long)";
    }
};

TEST_CASE("Oracle large strings as long", "[oracle][compatibility]")
{
    session sql(backEnd, connectString);
    long_table_creator creator(sql);

    const std::string::size_type max = 32768;
    std::string in(max, 'X');

    sql << "insert into soci_test values(:l)", use(in);

    std::string out;
    sql << "select l from soci_test", into(out);

    CHECK(out.size() == max);
    CHECK(in == out);
}

// test for modifiable and const use elements
TEST_CASE("Oracle const and modifiable parameters", "[oracle][use]")
{
    session sql(backEnd, connectString);

    int i = 7;
    sql << "begin "
        "select 2 * :i into :i from dual; "
        "end;", use(i);
    CHECK(i == 14);

    const int j = 7;
    try
    {
        sql << "begin "
            "select 2 * :i into :i from dual;"
            " end;", use(j);

        FAIL("expected exception not thrown");
    }
    catch (soci_error const & e)
    {
        const std::string msg = e.what();
        CHECK(msg == "Attempted modification of const use element");
    }
}

struct longlong_table_creator : table_creator_base
{
    longlong_table_creator(session & sql)
        : table_creator_base(sql)
    {
        sql << "create table soci_test(val number(20))";
    }
};

// long long test
TEST_CASE("Oracle long long", "[oracle][longlong]")
{
    {
        session sql(backEnd, connectString);

        longlong_table_creator tableCreator(sql);

        long long v1 = 1000000000000LL;
        sql << "insert into soci_test(val) values(:val)", use(v1);

        long long v2 = 0LL;
        sql << "select val from soci_test", into(v2);

        CHECK(v2 == v1);
    }

    // vector<long long>
    {
        session sql(backEnd, connectString);

        longlong_table_creator tableCreator(sql);

        std::vector<long long> v1;
        v1.push_back(1000000000000LL);
        v1.push_back(1000000000001LL);
        v1.push_back(1000000000002LL);
        v1.push_back(1000000000003LL);
        v1.push_back(1000000000004LL);

        sql << "insert into soci_test(val) values(:val)", use(v1);

        std::vector<long long> v2(10);
        sql << "select val from soci_test order by val desc", into(v2);

        REQUIRE(v2.size() == 5);
        CHECK(v2[0] == 1000000000004LL);
        CHECK(v2[1] == 1000000000003LL);
        CHECK(v2[2] == 1000000000002LL);
        CHECK(v2[3] == 1000000000001LL);
        CHECK(v2[4] == 1000000000000LL);
    }
}

//
// Support for soci Common Tests
//

struct table_creator_one : public table_creator_base
{
    table_creator_one(session & sql)
        : table_creator_base(sql)
    {
        sql << "create table soci_test(id number(10,0), val number(4,0), c char, "
                 "str varchar2(20), sh number, ul number, d number, "
                 "num76 numeric(7,6), "
                 "tm date, i1 number, i2 number, i3 number, name varchar2(20))";
    }
};

struct table_creator_two : public table_creator_base
{
    table_creator_two(session & sql)
        : table_creator_base(sql)
    {
        sql  << "create table soci_test(num_float number, num_int numeric(4,0),"
                    " name varchar2(20), sometime date, chr char)";
    }
};

struct table_creator_three : public table_creator_base
{
    table_creator_three(session & sql)
        : table_creator_base(sql)
    {
        sql << "create table soci_test(name varchar2(100) not null, "
            "phone varchar2(15))";
    }
};

struct table_creator_four : public table_creator_base
{
    table_creator_four(session & sql)
        : table_creator_base(sql)
    {
        sql << "create table soci_test(val number)";
    }
};

class test_context :public test_context_base
{
public:
    test_context(backend_factory const &backEnd,
                std::string const &connectString)
        : test_context_base(backEnd, connectString) {}

    table_creator_base* table_creator_1(session& s) const
    {
        return new table_creator_one(s);
    }

    table_creator_base* table_creator_2(session& s) const
    {
        return new table_creator_two(s);
    }

    table_creator_base* table_creator_3(session& s) const
    {
        return new table_creator_three(s);
    }

    table_creator_base* table_creator_4(session& s) const
    {
        return new table_creator_four(s);
    }

    std::string to_date_time(std::string const &datdt_string) const
    {
        return "to_date('" + datdt_string + "', 'YYYY-MM-DD HH24:MI:SS')";
    }
};

int main(int argc, char** argv)
{
#ifdef _MSC_VER
    // Redirect errors, unrecoverable problems, and assert() failures to STDERR,
    // instead of debug message window.
    // This hack is required to run assert()-driven tests by Buildbot.
    // NOTE: Comment this 2 lines for debugging with Visual C++ debugger to catch assertions inside.
    _CrtSetReportMode(_CRT_ERROR, _CRTDBG_MODE_FILE);
    _CrtSetReportFile(_CRT_ERROR, _CRTDBG_FILE_STDERR);
#endif //_MSC_VER

    if (argc >= 2)
    {
        connectString = argv[1];

        // Replace the connect string with the process name to ensure that
        // CATCH uses the correct name in its messages.
        argv[1] = argv[0];

        argc--;
        argv++;
    }
    else
    {
        std::cout << "usage: " << argv[0]
            << " connectstring [test-arguments...]\n"
            << "example: " << argv[0]
            << " \'service=orcl user=scott password=tiger\'\n";
        std::exit(1);
    }

    test_context tc(backEnd, connectString);

    return Catch::Session().run(argc, argv);
}
