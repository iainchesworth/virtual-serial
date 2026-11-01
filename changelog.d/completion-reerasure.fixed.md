Re-erasing an rvalue completion handler now adopts it instead of wrapping it a
second time, removing one allocation and one redundant post per asynchronous
operation.
