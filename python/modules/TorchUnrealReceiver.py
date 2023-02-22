import itertools

a = [0,1,2,3,4,5,6,7,8,9]
b = [0,1,2,3,4,5,6,7,8,9]
c = [0,1,2,3,4,5,6,7,8,9]

for i in itertools.product(a,b,c) :
  print(i)
