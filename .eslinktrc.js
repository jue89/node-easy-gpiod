module.exports = {
	'env': {
		'browser': true,
		'es2021': true
	},
	'extends': 'eslint:recommended',
	'overrides': [
	],
	'parserOptions': {
		'ecmaVersion': 'latest',
		'sourceType': 'module'
	},
	'rules': {
		'indent': [
			'error',
			'tab'
		],
		'linebreak-style': [
			'error',
			'unix'
		],
		'quotes': [
			'error',
			'single'
		],
		'semi': [
			'error',
			'always'
		],
		'eol-last': [
			'error',
			'always'
		],
		'space-before-blocks': [
			'error',
			'always'
		],
		'arrow-spacing': [
			'error'
		],
		'semi-spacing': [
			'error'
		],
		'space-before-function-paren': [
			'error',
			'always'
		],
		'keyword-spacing': [
			'error'
		],
		'no-trailing-spaces': [
			'error'
		],
		'space-infix-ops': [
			'error'
		],
		'no-unused-vars': [
			'error',
			{ 'args': 'none' }
		]
	}
};