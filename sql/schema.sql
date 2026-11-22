-- Runs on every startup, so every statement must be idempotent.
-- After changing a table definition, run `./stocksocial --reset`.

-- Entities

CREATE TABLE IF NOT EXISTS users (
    username VARCHAR(32) PRIMARY KEY,
    password TEXT NOT NULL -- salted hash
);

CREATE TABLE IF NOT EXISTS portfolios (
    username VARCHAR(32) REFERENCES users ON DELETE CASCADE,
    name     VARCHAR(64),
    cash     NUMERIC(15,2) NOT NULL DEFAULT 0 CHECK (cash >= 0),
    PRIMARY KEY (username, name)
);

CREATE TABLE IF NOT EXISTS stocks (
    symbol VARCHAR(8) PRIMARY KEY
);

CREATE TABLE IF NOT EXISTS daily_prices (
    symbol      VARCHAR(8) REFERENCES stocks ON DELETE CASCADE,
    "timestamp" DATE,
    open        DOUBLE PRECISION, -- a few historical rows only have close
    high        DOUBLE PRECISION,
    low         DOUBLE PRECISION,
    close       DOUBLE PRECISION NOT NULL,
    volume      BIGINT NOT NULL CHECK (volume >= 0),
    PRIMARY KEY (symbol, "timestamp")
);

CREATE TABLE IF NOT EXISTS lists (
    id         INTEGER GENERATED ALWAYS AS IDENTITY PRIMARY KEY,
    creator    VARCHAR(32) NOT NULL REFERENCES users ON DELETE CASCADE,
    name       VARCHAR(64) NOT NULL,
    visibility VARCHAR(8) NOT NULL DEFAULT 'private'
               CHECK (visibility IN ('private', 'shared', 'public'))
);

-- Relationships

CREATE TABLE IF NOT EXISTS friends (
    requester    VARCHAR(32) REFERENCES users ON DELETE CASCADE,
    recipient    VARCHAR(32) REFERENCES users ON DELETE CASCADE,
    status       VARCHAR(8) NOT NULL DEFAULT 'pending'
                 CHECK (status IN ('pending', 'accepted', 'rejected', 'removed')),
    last_changed TIMESTAMPTZ NOT NULL DEFAULT now(),
    PRIMARY KEY (requester, recipient),
    CHECK (requester <> recipient)
);

-- At most one row per pair: (A, B) and (B, A) can't both exist.
CREATE UNIQUE INDEX IF NOT EXISTS friends_unordered_pair
    ON friends (LEAST(requester, recipient), GREATEST(requester, recipient));

CREATE TABLE IF NOT EXISTS holds (
    username       VARCHAR(32),
    portfolio_name VARCHAR(64),
    symbol         VARCHAR(8) REFERENCES stocks,
    quantity       INTEGER NOT NULL CHECK (quantity > 0),
    PRIMARY KEY (username, portfolio_name, symbol),
    FOREIGN KEY (username, portfolio_name) REFERENCES portfolios ON DELETE CASCADE
);

CREATE TABLE IF NOT EXISTS contains (
    list_id  INTEGER REFERENCES lists ON DELETE CASCADE,
    symbol   VARCHAR(8) REFERENCES stocks,
    quantity INTEGER NOT NULL CHECK (quantity > 0),
    PRIMARY KEY (list_id, symbol)
);

CREATE TABLE IF NOT EXISTS reviews (
    list_id      INTEGER REFERENCES lists ON DELETE CASCADE,
    username     VARCHAR(32) REFERENCES users ON DELETE CASCADE,
    content      VARCHAR(4000) NOT NULL,
    last_updated TIMESTAMPTZ NOT NULL DEFAULT now(),
    PRIMARY KEY (list_id, username)
);

CREATE TABLE IF NOT EXISTS shared_with (
    list_id  INTEGER REFERENCES lists ON DELETE CASCADE,
    username VARCHAR(32) REFERENCES users ON DELETE CASCADE,
    PRIMARY KEY (list_id, username)
);

-- Caching tables (derived data)

CREATE TABLE IF NOT EXISTS stock_stats (
    symbol     VARCHAR(8) REFERENCES stocks ON DELETE CASCADE,
    start_date DATE,
    end_date   DATE,
    cov        DOUBLE PRECISION,
    beta       DOUBLE PRECISION,
    PRIMARY KEY (symbol, start_date, end_date)
);

CREATE TABLE IF NOT EXISTS pair_stats (
    symbol1     VARCHAR(8) REFERENCES stocks ON DELETE CASCADE,
    symbol2     VARCHAR(8) REFERENCES stocks ON DELETE CASCADE,
    start_date  DATE,
    end_date    DATE,
    covariance  DOUBLE PRECISION,
    correlation DOUBLE PRECISION,
    PRIMARY KEY (symbol1, symbol2, start_date, end_date),
    CHECK (symbol1 < symbol2)
);

-- Functions

-- Most recent close for a symbol, or NULL if unknown. Fast: it walks the
-- (symbol, timestamp) primary key index backwards and stops at the first row.
CREATE OR REPLACE FUNCTION latest_close(sym VARCHAR) RETURNS DOUBLE PRECISION AS $$
    SELECT close FROM daily_prices WHERE symbol = sym ORDER BY "timestamp" DESC LIMIT 1
$$ LANGUAGE sql STABLE;

-- Whether user `who` can see list `lid`.
CREATE OR REPLACE FUNCTION can_access(lid INTEGER, who VARCHAR) RETURNS BOOLEAN AS $$
    SELECT ((SELECT creator FROM lists WHERE id = lid) = who)
        OR EXISTS (
            SELECT 1 FROM lists
            WHERE id = lid
              AND (visibility = 'public'
                OR (visibility = 'shared'
                    AND EXISTS (SELECT 1 FROM shared_with WHERE list_id = lid AND username = who))))
$$ LANGUAGE sql STABLE;

-- Triggers

-- Lists can only be shared with friends of the creator.
CREATE OR REPLACE FUNCTION check_share_is_friend() RETURNS TRIGGER AS $$
DECLARE
    list_creator VARCHAR(32);
BEGIN
    SELECT creator INTO list_creator FROM lists WHERE id = NEW.list_id;
    IF NOT EXISTS (
        SELECT 1 FROM friends
        WHERE LEAST(requester, recipient) = LEAST(list_creator, NEW.username)
          AND GREATEST(requester, recipient) = GREATEST(list_creator, NEW.username)
          AND status = 'accepted'
    ) THEN
        RAISE EXCEPTION 'Lists can only be shared with friends';
    END IF;
    RETURN NEW;
END;
$$ LANGUAGE plpgsql;

CREATE OR REPLACE TRIGGER shared_with_requires_friendship
    BEFORE INSERT OR UPDATE ON shared_with
    FOR EACH ROW EXECUTE FUNCTION check_share_is_friend();

-- When a friendship ends, unshare lists between the two users, since lists can
-- only be shared with friends.
CREATE OR REPLACE FUNCTION unshare_on_unfriend() RETURNS TRIGGER AS $$
BEGIN
    DELETE FROM shared_with sw USING lists l
    WHERE sw.list_id = l.id
      AND ((l.creator = OLD.requester AND sw.username = OLD.recipient)
        OR (l.creator = OLD.recipient AND sw.username = OLD.requester));
    RETURN NULL;
END;
$$ LANGUAGE plpgsql;

CREATE OR REPLACE TRIGGER friends_unshare_on_unfriend
    AFTER UPDATE OF status ON friends
    FOR EACH ROW WHEN (OLD.status = 'accepted' AND NEW.status <> 'accepted')
    EXECUTE FUNCTION unshare_on_unfriend();
