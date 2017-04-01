select load_extension("./libvoikko_fts");
select head from fts where head match 'mies'; 