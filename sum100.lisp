((lambda ()
  (defun sum (n acc)
    (if (eq n 0)
        acc
        (sum (- n 1) (+ n acc))))
  (sum 100 0)))
