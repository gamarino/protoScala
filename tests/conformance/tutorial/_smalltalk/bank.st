"-- bank.st: the Smalltalk side of tutorial chapter 15.5.1 --"
Object subclass: #Account instanceVariableNames: 'balance'.
Error subclass: #InsufficientFunds.
Account >> initialize
  balance := 0.

Account >> balance ^ balance.
Account >> deposit: amount
  balance := balance + amount.
  ^ balance.

Account >> withdraw: amount
  amount > balance ifTrue: [ ^ InsufficientFunds new signal: 'insufficient funds' ].
  balance := balance - amount.
  ^ balance.

Account >> transfer: amount to: other
  self withdraw: amount.
  ^ other deposit: amount.

Account >> report: aFunction
  ^ aFunction apply(balance).
