CREATE TABLE INT8_TBL(q1 int8, q2 int8);
INSERT INTO INT8_TBL VALUES('  123   ','  456');
INSERT INTO INT8_TBL VALUES('123   ','4567890123456789');
INSERT INTO INT8_TBL VALUES('4567890123456789','123');
INSERT INTO INT8_TBL VALUES(+4567890123456789,'4567890123456789');
INSERT INTO INT8_TBL VALUES('+4567890123456789','-4567890123456789');
INSERT INTO INT8_TBL(q1) VALUES ('      ');
INSERT INTO INT8_TBL(q1) VALUES ('xxx');
INSERT INTO INT8_TBL(q1) VALUES ('3908203590239580293850293850329485');
INSERT INTO INT8_TBL(q1) VALUES ('-1204982019841029840928340329840934');
INSERT INTO INT8_TBL(q1) VALUES ('- 123');
INSERT INTO INT8_TBL(q1) VALUES ('  345     5');
INSERT INTO INT8_TBL(q1) VALUES ('');

CREATE TABLE INT4_TBL(f1 int4);
INSERT INTO INT4_TBL(f1) VALUES ('   0  ');
INSERT INTO INT4_TBL(f1) VALUES ('123456     ');
INSERT INTO INT4_TBL(f1) VALUES ('    -123456');
INSERT INTO INT4_TBL(f1) VALUES ('34.5');
INSERT INTO INT4_TBL(f1) VALUES ('2147483647');
INSERT INTO INT4_TBL(f1) VALUES ('-2147483647');
INSERT INTO INT4_TBL(f1) VALUES ('1000000000000');
INSERT INTO INT4_TBL(f1) VALUES ('asdf');
INSERT INTO INT4_TBL(f1) VALUES ('     ');
INSERT INTO INT4_TBL(f1) VALUES ('   asdf   ');
INSERT INTO INT4_TBL(f1) VALUES ('- 1234');
INSERT INTO INT4_TBL(f1) VALUES ('123       5');
INSERT INTO INT4_TBL(f1) VALUES ('');


explain (verbose, costs off)
select * from int8_tbl i8 left join lateral
  (select *, i8.q2 from int4_tbl where false) ss on true;


set optimizer_print_missing_stats = off;
CREATE TABLE tenk1 (
	unique1		int4,
	unique2		int4,
	two			int4,
	four		int4,
	ten			int4,
	twenty		int4,
	hundred		int4,
	thousand	int4,
	twothousand	int4,
	fivethous	int4,
	tenthous	int4,
	odd			int4,
	even		int4,
	stringu1	name,
	stringu2	name,
	string4		name
) WITH OIDS;


CREATE TABLE tenk2 (
	unique1 	int4,
	unique2 	int4,
	two 	 	int4,
	four 		int4,
	ten			int4,
	twenty 		int4,
	hundred 	int4,
	thousand 	int4,
	twothousand int4,
	fivethous 	int4,
	tenthous	int4,
	odd			int4,
	even		int4,
	stringu1	name,
	stringu2	name,
	string4		name
);

INSERT INTO tenk1 (
    unique1, unique2, two, four, ten, twenty, hundred, thousand,
    twothousand, fivethous, tenthous, odd, even, stringu1, stringu2, string4
) VALUES
(1, 101, 2, 4, 10, 20, 100, 1000, 2000, 5000, 10000, 1, 2, 'str_u1_1', 'str_u2_1', 'str_4_1'),
(2, 102, 4, 8, 20, 40, 200, 2000, 4000, 10000, 20000, 3, 4, 'str_u1_2', 'str_u2_2', 'str_4_2'),
(3, 103, 6, 12, 30, 60, 300, 3000, 6000, 15000, 30000, 5, 6, 'str_u1_3', 'str_u2_3', 'str_4_3'),
(4, 104, 8, 16, 40, 80, 400, 4000, 8000, 20000, 40000, 7, 8, 'str_u1_4', 'str_u2_4', 'str_4_4'),
(5, 105, 10, 20, 50, 100, 500, 5000, 10000, 25000, 50000, 9, 10, 'str_u1_5', 'str_u2_5', 'str_4_5'),
(6, 106, 12, 24, 60, 120, 600, 6000, 12000, 30000, 60000, 11, 12, 'str_u1_6', 'str_u2_6', 'str_4_6'),
(7, 107, 14, 28, 70, 140, 700, 7000, 14000, 35000, 70000, 13, 14, 'str_u1_7', 'str_u2_7', 'str_4_7'),
(8, 108, 16, 32, 80, 160, 800, 8000, 16000, 40000, 80000, 15, 16, 'str_u1_8', 'str_u2_8', 'str_4_8'),
(9, 109, 18, 36, 90, 180, 900, 9000, 18000, 45000, 90000, 17, 18, 'str_u1_9', 'str_u2_9', 'str_4_9'),
(10, 110, 20, 40, 100, 200, 1000, 10000, 20000, 50000, 100000, 19, 20, 'str_u1_10', 'str_u2_10', 'str_4_10'),

(11, 111, 22, 44, 110, 220, 1100, 11000, 22000, 55000, 110000, 21, 22, 'str_u1_11', 'str_u2_11', 'str_4_11'),
(12, 112, 24, 48, 120, 240, 1200, 12000, 24000, 60000, 120000, 23, 24, 'str_u1_12', 'str_u2_12', 'str_4_12'),
(13, 113, 26, 52, 130, 260, 1300, 13000, 26000, 65000, 130000, 25, 26, 'str_u1_13', 'str_u2_13', 'str_4_13'),
(14, 114, 28, 56, 140, 280, 1400, 14000, 28000, 70000, 140000, 27, 28, 'str_u1_14', 'str_u2_14', 'str_4_14'),
(15, 115, 30, 60, 150, 300, 1500, 15000, 30000, 75000, 150000, 29, 30, 'str_u1_15', 'str_u2_15', 'str_4_15'),
(16, 116, 32, 64, 160, 320, 1600, 16000, 32000, 80000, 160000, 31, 32, 'str_u1_16', 'str_u2_16', 'str_4_16'),
(17, 117, 34, 68, 170, 340, 1700, 17000, 34000, 85000, 170000, 33, 34, 'str_u1_17', 'str_u2_17', 'str_4_17'),
(18, 118, 36, 72, 180, 360, 1800, 18000, 36000, 90000, 180000, 35, 36, 'str_u1_18', 'str_u2_18', 'str_4_18'),
(19, 119, 38, 76, 190, 380, 1900, 19000, 38000, 95000, 190000, 37, 38, 'str_u1_19', 'str_u2_19', 'str_4_19'),
(20, 120, 40, 80, 200, 400, 2000, 20000, 40000, 100000, 200000, 39, 40, 'str_u1_20', 'str_u2_20', 'str_4_20'),

(21, 121, 42, 84, 210, 420, 2100, 21000, 42000, 105000, 210000, 41, 42, 'str_u1_21', 'str_u2_21', 'str_4_21'),
(22, 122, 44, 88, 220, 440, 2200, 22000, 44000, 110000, 220000, 43, 44, 'str_u1_22', 'str_u2_22', 'str_4_22'),
(23, 123, 46, 92, 230, 460, 2300, 23000, 46000, 115000, 230000, 45, 46, 'str_u1_23', 'str_u2_23', 'str_4_23'),
(24, 124, 48, 96, 240, 480, 2400, 24000, 48000, 120000, 240000, 47, 48, 'str_u1_24', 'str_u2_24', 'str_4_24'),
(25, 125, 50, 100, 250, 500, 2500, 25000, 50000, 125000, 250000, 49, 50, 'str_u1_25', 'str_u2_25', 'str_4_25'),
(26, 126, 52, 104, 260, 520, 2600, 26000, 52000, 130000, 260000, 51, 52, 'str_u1_26', 'str_u2_26', 'str_4_26'),
(27, 127, 54, 108, 270, 540, 2700, 27000, 54000, 135000, 270000, 53, 54, 'str_u1_27', 'str_u2_27', 'str_4_27'),
(28, 128, 56, 112, 280, 560, 2800, 28000, 56000, 140000, 280000, 55, 56, 'str_u1_28', 'str_u2_28', 'str_4_28'),
(29, 129, 58, 116, 290, 580, 2900, 29000, 58000, 145000, 290000, 57, 58, 'str_u1_29', 'str_u2_29', 'str_4_29'),
(30, 130, 60, 120, 300, 600, 3000, 30000, 60000, 150000, 300000, 59, 60, 'str_u1_30', 'str_u2_30', 'str_4_30');

-- Insert 30 rows into tenk2
INSERT INTO tenk2 (
    unique1, unique2, two, four, ten, twenty, hundred, thousand,
    twothousand, fivethous, tenthous, odd, even, stringu1, stringu2, string4
) VALUES
(101, 201, 3, 6, 15, 30, 150, 1500, 3000, 7500, 15000, 2, 3, 'str2_u1_1', 'str2_u2_1', 'str2_4_1'),
(102, 202, 6, 12, 30, 60, 300, 3000, 6000, 15000, 30000, 4, 5, 'str2_u1_2', 'str2_u2_2', 'str2_4_2'),
(103, 203, 9, 18, 45, 90, 450, 4500, 9000, 22500, 45000, 6, 7, 'str2_u1_3', 'str2_u2_3', 'str2_4_3'),
(104, 204, 12, 24, 60, 120, 600, 6000, 12000, 30000, 60000, 8, 9, 'str2_u1_4', 'str2_u2_4', 'str2_4_4'),
(105, 205, 15, 30, 75, 150, 750, 7500, 15000, 37500, 75000, 10, 11, 'str2_u1_5', 'str2_u2_5', 'str2_4_5'),
(106, 206, 18, 36, 90, 180, 900, 9000, 18000, 45000, 90000, 12, 13, 'str2_u1_6', 'str2_u2_6', 'str2_4_6'),
(107, 207, 21, 42, 105, 210, 1050, 10500, 21000, 52500, 105000, 14, 15, 'str2_u1_7', 'str2_u2_7', 'str2_4_7'),
(108, 208, 24, 48, 120, 240, 1200, 12000, 24000, 60000, 120000, 16, 17, 'str2_u1_8', 'str2_u2_8', 'str2_4_8'),
(109, 209, 27, 54, 135, 270, 1350, 13500, 27000, 67500, 135000, 18, 19, 'str2_u1_9', 'str2_u2_9', 'str2_4_9'),
(110, 210, 30, 60, 150, 300, 1500, 15000, 30000, 75000, 150000, 20, 21, 'str2_u1_10', 'str2_u2_10', 'str2_4_10'),

(111, 211, 33, 66, 165, 330, 1650, 16500, 33000, 82500, 165000, 22, 23, 'str2_u1_11', 'str2_u2_11', 'str2_4_11'),
(112, 212, 36, 72, 180, 360, 1800, 18000, 36000, 90000, 180000, 24, 25, 'str2_u1_12', 'str2_u2_12', 'str2_4_12'),
(113, 213, 39, 78, 195, 390, 1950, 19500, 39000, 97500, 195000, 26, 27, 'str2_u1_13', 'str2_u2_13', 'str2_4_13'),
(114, 214, 42, 84, 210, 420, 2100, 21000, 42000, 105000, 210000, 28, 29, 'str2_u1_14', 'str2_u2_14', 'str2_4_14'),
(115, 215, 45, 90, 225, 450, 2250, 22500, 45000, 112500, 225000, 30, 31, 'str2_u1_15', 'str2_u2_15', 'str2_4_15');
insert into tenk2 (select * from tenk2);
BEGIN;

DECLARE foo1 SCROLL CURSOR FOR SELECT * FROM tenk1 ORDER BY unique2;

DECLARE foo2 CURSOR FOR SELECT * FROM tenk2 ORDER BY 1,2,3,4;

DECLARE foo3 SCROLL CURSOR FOR SELECT * FROM tenk1 ORDER BY unique2;

DECLARE foo4 CURSOR FOR SELECT * FROM tenk2 ORDER BY 1,2,3,4;

DECLARE foo5 SCROLL CURSOR FOR SELECT * FROM tenk1 ORDER BY unique2;

DECLARE foo6 CURSOR FOR SELECT * FROM tenk2 ORDER BY 1,2,3,4;

DECLARE foo7 SCROLL CURSOR FOR SELECT * FROM tenk1 ORDER BY unique2;

DECLARE foo8 CURSOR FOR SELECT * FROM tenk2 ORDER BY 1,2,3,4;

DECLARE foo9 SCROLL CURSOR FOR SELECT * FROM tenk1 ORDER BY unique2;

DECLARE foo10 CURSOR FOR SELECT * FROM tenk2 ORDER BY 1,2,3,4;

DECLARE foo11 SCROLL CURSOR FOR SELECT * FROM tenk1 ORDER BY unique2;

DECLARE foo12 CURSOR FOR SELECT * FROM tenk2 ORDER BY 1,2,3,4;

DECLARE foo13 SCROLL CURSOR FOR SELECT * FROM tenk1 ORDER BY unique2;

DECLARE foo14 CURSOR FOR SELECT * FROM tenk2 ORDER BY 1,2,3,4;

DECLARE foo15 SCROLL CURSOR FOR SELECT * FROM tenk1 ORDER BY unique2;

DECLARE foo16 CURSOR FOR SELECT * FROM tenk2 ORDER BY 1,2,3,4;

DECLARE foo17 SCROLL CURSOR FOR SELECT * FROM tenk1 ORDER BY unique2;

DECLARE foo18 CURSOR FOR SELECT * FROM tenk2 ORDER BY 1,2,3,4;

DECLARE foo19 SCROLL CURSOR FOR SELECT * FROM tenk1 ORDER BY unique2;

DECLARE foo20 CURSOR FOR SELECT * FROM tenk2 ORDER BY 1,2,3,4;

DECLARE foo21 SCROLL CURSOR FOR SELECT * FROM tenk1 ORDER BY unique2;

DECLARE foo22 CURSOR FOR SELECT * FROM tenk2 ORDER BY 1,2,3,4;

DECLARE foo23 SCROLL CURSOR FOR SELECT * FROM tenk1 ORDER BY unique2;

FETCH 1 in foo1;

FETCH 2 in foo2;

FETCH 3 in foo3;

FETCH 4 in foo4;

FETCH 5 in foo5;

FETCH 6 in foo6;

FETCH 7 in foo7;

FETCH 8 in foo8;

FETCH 9 in foo9;

FETCH 10 in foo10;

FETCH 11 in foo11;

FETCH 12 in foo12;

FETCH 13 in foo13;

FETCH 14 in foo14;

FETCH 15 in foo15;

FETCH 16 in foo16;

FETCH 17 in foo17;

FETCH 18 in foo18;

FETCH 19 in foo19;

FETCH 20 in foo20;

FETCH 21 in foo21;

FETCH 22 in foo22;

FETCH 23 in foo23;


CLOSE foo1;

CLOSE foo2;

CLOSE foo3;

CLOSE foo4;

CLOSE foo5;

CLOSE foo6;

CLOSE foo7;

CLOSE foo8;

CLOSE foo9;

CLOSE foo10;

CLOSE foo11;

CLOSE foo12;




CREATE TABLE INT4_TBL(f1 int4);

INSERT INTO INT4_TBL(f1) VALUES ('   0  ');

INSERT INTO INT4_TBL(f1) VALUES ('123456     ');

INSERT INTO INT4_TBL(f1) VALUES ('    -123456');

INSERT INTO INT4_TBL(f1) VALUES ('34.5');

-- largest and smallest values
INSERT INTO INT4_TBL(f1) VALUES ('2147483647');

INSERT INTO INT4_TBL(f1) VALUES ('-2147483647');


explain (verbose, costs off)
select * from int4_tbl where
  (case when f1 in (select unique1 from tenk1 a) then f1 else null end) in
  (select ten from tenk1 b);
select * from int4_tbl where
  (case when f1 in (select unique1 from tenk1 a) then f1 else null end) in
  (select ten from tenk1 b);


