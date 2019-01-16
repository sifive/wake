;;; wake-mode.el --- mode for working with wake files

;; A major mode for editing wake files

;;; Code:

;;; Customization
(defgroup wake nil
  "Configuration for wake-mode."
  :prefix "wake-"
  :group 'wp)

(defcustom wake-tab-width 2
  "Width of a tab for wake."
  :group 'wake
  :type '(integer))

;;; functions defined in wake.prim
(defvar wake-def-prims
  '("first"
    "second"
    "empty"
    "head"
    "tail"
    "map"
    "mapPartial"
    "foldl"
    "scanl"
    "foldr"
    "scanr"
    "reverse"
    "flatten"
    "len"
    "splitAt"
    "take"
    "drop"
    "atOpt"
    "at"
    "splitUntil"
    "takeUntil"
    "dropUntil"
    "find"
    "exists"
    "forall"
    "splitBy"
    "filter"
    "transpose"
    "sortBy"
    "distinctBy"
    "cmp"
    "tab"
    "seq"
    "zip"
    "unzip"
    "lvector"
    "tvector"
    "vempty"
    "vlen"
    "vlist"
    "vsplitAt"
    "vtake"
    "vdrop"
    "vatOpt"
    "vat"
    "vmap"
    "vseq"
    "vzip"
    "vunzip"
    "vreverse"
    "vfoldl"
    "vfoldr"
    "vfoldmap"
    "vfold"
    "vfind"
    "vsplitUntil"
    "vtakeUntil"
    "vdropUntil"
    "vexists"
    "vforall"
    "vsplitBy"
    "vfilter"
    "vunfoldl"
    "vscanl"
    "vscanr"
    "vscanmap"
    "vscan"
    "vsortBy"
    "vdistinctBy"
    "vcmp"
    "vtranspose"
    "vflatten"
    "vtab"
    "quote"
    "matches"
    "extract"
    "replace"
    "tokenize"
    "try"
    "tryelse"
    "raise"
    "tnew"
    "ltree"
    "ltreeMulti"
    "vtreeMulti"
    "vtree"
    "tlen"
    "tempty"
    "tinsert"
    "tinsertReplace"
    "tinsertMulti"
    "tsubset"
    "tdelete"
    "tfoldl"
    "tfoldr"
    "tfoldmap"
    "tfold"
    "tlist"
    "tappi"
    "tatOpt"
    "tat"
    "tsplitAt"
    "ttake"
    "tdrop"
    "tfind"
    "tsplitUntil"
    "ttakeUntil"
    "tdropUntil"
    "texists"
    "tforall"
    "tsplit"
    "tsplitBy"
    "tfilter"
    "tmin"
    "tmax"
    "tlowerGE"
    "tlowerGT"
    "tupperLT"
    "tupperLE"
    "tcontains"
    "tdistinctBy"
    "tunion"
    "tunionMulti"
    "tsubtract"
    "tintersect"
    "tshape"
    "cat"
    "catWith"
    "explode"
    "strbase"
    "intbase"
    "str"
    "int"
    "code2str"
    "str2code"
    "bin2str"
    "str2bin"
    "version"
    "sNFC"
    "sNFKC"
    "scaseNFKC"
    "scmpNFC"
    "scmpNFKC"
    "scasecmpNFKC"
    "scmpRaw"
    "lt"
    "eq"
    "root"
    "sqrt"
    "abs"
    "xor"
    "and"
    "or"
    "gcd"
    "lcm"
    "powm"
    "icmp"
    "min"
    "max"
    "print"
    "println"
    "dbg"
    "format"
    "waitOne"
    "waitAll"
    "pkgConfig"
    "cflags"
    "libs"
    "compileC"
    "linkO"
    "path"
    "cp"
    "installAs"
    "installIn"
    "sources"
    "files"
    "jobcache"
    "launch"
    "kill"
    "status"
    "stdout"
    "stderr"
    "inputs"
    "outputs"
    "rawinputs"
    "rawoutputs"
    "output"
    "finish"
    "hashpair"
    "hashname"
    "hashcode"
    "uncached_manual_job"
    "cached_manual_job"
    "uncached_fuse_job"
    "cached_fuse_job"
    "environment"
    "job"
    "always_job"
    "manual_job"
    "volatile_job"
    "read"
    "write"
    "mkdir"
    "simplify"
    "relative"
    "whichIn"
    "which"
    "workspace"
    ))

(defvar wake-keyword
  '("def" "global" "publish" "subscribe" "if" "then" "else" "here" "prim" "match" "data" "memoize"))

(defvar wake-prim-regex (regexp-opt wake-def-prims 'words))
(defvar wake-keyword-regex (regexp-opt wake-keyword 'words))
(defvar wake-identifier "\\([a-z_][A-Za-z0-9_]*\\)")
(defvar wake-op-regex "\\([.!&|,+~*/%<>$^-][.!&|,+~*/%<>$=^-]*\\|=[.!&|,+~*/%<>$=^-]+\\)")
(defvar wake-type (concat "\\([A-Z][A-Za-z0-9_]*" "\\|" wake-op-regex "\\)"))
(defvar wake-variable-start "\\(publish\\)")
(defvar wake-operator-arg-list (concat "\\(" wake-identifier "\s*\\)?" wake-op-regex "\s*" wake-identifier))

(defvar wake-font-lock-keywords
  `(;; publish
    (,(concat "publish\s+" wake-identifier "\s*=[\s\n\r]")
     (1 font-lock-warning-face))

    ;; operator defs
    (,(concat "\\(global\s+\\)?" "def\s+" wake-operator-arg-list)
     (4 font-lock-function-name-face))

    ;; normal defs
    (,(concat "\\(global\s+\\)?" "def\s+" wake-identifier "\\(\s+" wake-identifier "\\|\s*=[(A-Za-z0-9\s\r\n]\\|\s*\(\\)")
     (2 font-lock-function-name-face))

    ;;;; normal defs
    ;;(,(concat "\\(global\s+\\)?" "def\s+" wake-identifier "\\(\s+" wake-identifier "\\|\s*=[\s\n\r]\\)")
    ;; (2 font-lock-function-name-face))

    ;; operator datas
    (,(concat "\\(global\s+\\)?" "data\s+" wake-operator-arg-list)
     (4 font-lock-type-face))

    ;; normal datas
    (,(concat "\\(global\s+\\)?" "data\s+" wake-identifier "\\(\s+" wake-identifier "\\|\s*=[\s\n\r]\\)")
     (2 font-lock-type-face))

    ;; keywords
    (,wake-keyword-regex . font-lock-keyword-face)

    ;; functions in wake.prim
    (,wake-prim-regex . font-lock-builtin-face)

    ;; operators in wake.prim
    (,wake-op-regex . font-lock-variable-name-face)

    ;; types
    (,(concat "\\<" wake-type "\\>") . font-lock-reference-face)

    ;; Literals
    ("\\<\\([0-9]+\\|0[xX][0-9A-Fa-f_]+\\)\\>"
     (1 font-lock-constant-face))
    ))

(defun start-of-indent-block ()
  "indent if any of the following conditions are met:
1. previous line ends with 'if', 'then', or 'else'
2. previous line ends with '`something` ='
3. previous line contains a 'match'
"
  (save-excursion
    (beginning-of-line-text)
    (backward-word)
    (beginning-of-line)

    ;; keep moving to previous line until we reach a non-comment
    (while (and (looking-at-p "\s*#") (not (bobp)))
      (backward-word)
      (beginning-of-line)
      )
    (beginning-of-line-text)

    (or
     (looking-at-p "\\(then\\|else\\|if\\)\s*$")
     (looking-at-p ".*[A-Za-z0-9_)(\s]=\s*$")
     (looking-at-p ".*\\<match\\>.*$")
     )
    )
  )

(defun end-of-indent-block ()
  "dedent if any of the following conditions are met:
1. previous line is NOT 'if', 'then', 'else', or 'def'
2. previous line does NOT contain '`something` = `something`'
"
  (let (no)
    (save-excursion
      ;; go to beginning of previous line
      (beginning-of-line-text)
      (backward-word)
      (beginning-of-line)

      ;; keep moving to previous line until we reach a non-comment
      (while (and (looking-at-p "\s*#") (not (bobp)))
        (backward-word)
        (beginning-of-line)
        )
      (beginning-of-line-text)

      ;; do not dedent if we are after if/then/def
      (setq no (or
                (looking-at-p "\\(if\\|then\\|def\\)\s+")
                (looking-at-p ".*\\_<=\\_>.*")
                ))
      )

    (not no)
    )
  )

(defun previous-line-indent ()
  (save-excursion
    (beginning-of-line-text)
    (backward-word)
    (beginning-of-line)

    ;; keep moving to previous line until we reach a non-comment
    (while (and (looking-at-p "\s*#") (not (bobp)))
      (backward-word)
      (beginning-of-line)
      )

    (current-indentation)
    )
  )

;; Indentation
(defun wake-possible-indentations ()
  (let
      ((default (default-wake-possible-indentations))
       (current (current-indentation)))
    (if (and
         (/= current default)
         (save-excursion
           (beginning-of-line)
           (not (looking-at-p "^\s*$")))
         )
        (number-sequence 0 current tab-width)
        (number-sequence 0 default tab-width)
      )
    )
  )

;;(defun wake-possible-indentations ()
;;  (number-sequence 0 (default-wake-possible-indentations) tab-width))

(defun default-wake-possible-indentations ()
  "Determine all possible indentations for a given line."
  (save-excursion
  (beginning-of-line)
  (let (indents)
    (cond ((or (bobp) (looking-at-p "\s*global"))
           (setq indents
                 0))

          ((start-of-indent-block)
           (setq indents
                 (+ (previous-line-indent) tab-width)))

          ((end-of-indent-block)
           (setq indents
                 (max (- (previous-line-indent) tab-width) 0)))

          (t
           (setq indents
                 (previous-line-indent)))
          )
    indents
    )))

(defvar wake--indents)
(defun wake-cycle-indents ()
  "Indent the current wake line.
Uses 'wake-possible-indentations' to determine all possible
indentations for the given line and then cycles through these on
repeated key presses."
  (interactive)
  (if (eq last-command 'indent-for-tab-command)
      (setq wake--indents (append (last wake--indents)
                                    (butlast wake--indents)))
    (setq wake--indents (wake-possible-indentations)))
  (indent-line-to (car (last wake--indents))))


(defvar wake-table
  (let ((table (make-syntax-table text-mode-syntax-table)))
    (modify-syntax-entry ?\# "<" table)
    (modify-syntax-entry ?\n ">" table)
    (modify-syntax-entry ?\{ "(}" table)
    (modify-syntax-entry ?\} "){" table)
    (modify-syntax-entry ?\( "()" table)
    (modify-syntax-entry ?\) ")(" table)
    (modify-syntax-entry ?_ "w" table)
    (modify-syntax-entry ?\\ "\\" table)
    (modify-syntax-entry ?\" "|" table)
    (modify-syntax-entry ?\' "|" table)
    table))

;;;###autoload
(define-derived-mode wake-mode text-mode "Wake"
  "Major mode for editing wake."
  (when wake-tab-width
    (setq tab-width wake-tab-width)) ;; Defined wake tab width

  ;; Set everything up
  (setq font-lock-defaults '(wake-font-lock-keywords))
  (setq-local indent-line-function 'wake-cycle-indents)
  (set-syntax-table wake-table)
  (setq-local comment-start "#")
  )

;;;###autoload
(add-to-list 'auto-mode-alist '("\\.wake\\'" . wake-mode))

(provide 'wake-mode)
;;; wake-mode.el ends here
