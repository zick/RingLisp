(defun self-eval? (exp)
  (if (eq exp nil)
      t
      (if (eq exp t)
          t
          (if (numberp exp)
              t
              nil))))

(defun cont (val stack)
  (if (eq stack nil)
      val
      (if (eq (car stack) 'evlis)
          ((lambda (in out e)
             (if (eq in nil)
                 ((lambda (fa)
                    (rapply (car fa) (cdr fa) e (cddr (cddr stack))))
                  (nreverse (cons val out)))
                 (reval (car in) e
                        (cons 'evlis (cons (cdr in)
                                           (cons (cons val out)
                                                 (cddr (cdr stack))))))))
           (cadr stack) (cadr (cdr stack)) (cadr (cddr stack)))
          (if (eq (car stack) 'if)
              (reval (if val (car (cadr stack)) (cadr (cadr stack)))
                     (car (cddr stack)) (cdr (cddr stack)))
              (if (eq (car stack) 'body)
                  (reval (car (cadr stack))
                         (car (cddr stack))
                         (if (eq (cdr (cadr stack)) nil)
                             (cdr (cddr stack))
                             (cons 'body (cons (cdr (cadr stack)) (cddr stack)))))
                  'undef-cont)))))

(defun appexpr (farg aarg body env binds stack)
  (if (eq farg nil)
      ((lambda (e)
         (reval (car body) e
                (if (eq (cdr body) nil)
                    stack
                    (cons 'body (cons (cdr body) (cons e stack))))))
       (update-env binds env nil))
      (appexpr (cdr farg) (cdr aarg) body env
               (cons (cons (car farg) (car aarg)) binds) stack)))

(defun search-bind (key binds)
  (if (eq binds nil)
      nil
      (if (eq (caar binds) key)
          (car binds)
          (search-bind key (cdr binds)))))

(defun rmbind (key binds acc)
  (if (eq binds nil)
      (nreverse acc)
      (rmbind key
              (cdr binds)
              (if (eq (caar binds) key)
                  acc
                  (cons (car binds) acc)))))

(defun update-env (binds env acc)
  (if (eq env nil)
      (append binds (nreverse acc))
      (if (eq binds nil)
          (append (nreverse acc) env)
          ((lambda (ret)
             (update-env (if ret (rmbind (car ret) binds nil) binds)
                         (cdr env)
                         (cons (if ret ret (car env))  acc)))
           (search-bind (caar env) binds)))))

(defun rapply (fn args env stack)
  (if (eq fn nil)
      'non-function
      (if (atom fn)
          (cont (funcall fn args) stack)
          (if (eq (car fn) 'lambda)
              (cont (fn args) stack)
              (appexpr (car fn) args (cdr fn) env nil stack)))))

(defun update-stack (k v stack)
  (if (eq (car stack) 'body)
      (cons 'body
            (cons (cadr stack)
                  (cons (cons (cons k v) (car (cddr stack)))
                        (cdr (cddr stack)))))
      stack))

(defun reval (exp env stack)
  (if (self-eval? exp)
      (cont exp stack)
      (if (symbolp exp)
          (cont (cdr (assoc exp env)) stack)
          (if (eq (car exp) 'quote)
              (cont (cadr exp) stack)
              (if (eq (car exp) 'lambda)
                  (cont (cdr exp) stack)
                  (if (eq (car exp) 'if)
                      (reval (cadr exp) env
                             (cons 'if (cons (cddr exp) (cons env stack))))
                      (if (eq (car exp) 'defun)
                          (cont (cadr exp)
                                (update-stack (cadr exp) (cddr exp) stack))
                          (reval
                           (car exp)
                           env
                           (cons 'evlis
                                 (cons (cdr exp)
                                       (cons nil
                                             (cons env stack))))))))))))
